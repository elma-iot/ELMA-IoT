"""Offline regression tests for interrupted OTA transfers."""
import unittest
import pathlib
import tempfile
from unittest.mock import Mock
from unittest.mock import patch
from urllib.parse import parse_qs, urlparse

from elma_flasher import DesignerJob, DesignerServer


class FakeDevice:
    def __init__(self, reboot=False):
        self.offset = 0
        self.session = ""
        self.failed = False
        self.reboot = reboot
        self.starts = 0
        self.finished = False

    def json_request(self, path, value):
        if path.endswith("/start"):
            self.session = value["sessionId"]
            self.offset = 0
            self.starts += 1
        elif path.endswith("/finish"):
            self.finished = True
        return {}

    def json(self, path):
        return {"upload": {"active": bool(self.session), "sessionId": self.session, "offset": self.offset}}

    def put_binary(self, path, chunk):
        offset = int(parse_qs(urlparse(path).query)["offset"][0])
        if offset == 8192 and not self.failed:
            self.failed = True
            self.offset = 0 if self.reboot else offset + len(chunk)
            if self.reboot:
                self.session = ""
            raise OSError("simulated Wi-Fi interruption")
        self.offset = offset + len(chunk)
        return {"upload": {"offset": self.offset}}


class OtaRecoveryTests(unittest.TestCase):
    def test_v10_classic_esp32_uses_legacy_slot_limit(self):
        server = DesignerServer(None)
        self.assertEqual(server.legacy_ota_capacity("esp32", "elma", "0.1.10"), 0x190000)
        self.assertIsNone(server.legacy_ota_capacity("esp32", "elma", "0.1.12"))
        self.assertIsNone(server.legacy_ota_capacity("esp32s3", "elma", "0.1.10"))

    def test_configuration_is_applied_after_legacy_reboot(self):
        client = Mock()
        client.json.side_effect = [OSError("restarting"), {"firmware": {"version": "0.1.42"}}]
        server = DesignerServer(None)
        with patch("elma_flasher.APP_VERSION", "0.1.42"), patch("elma_flasher.time.sleep"):
            server.apply_settings_after_legacy_ota(DesignerJob(), client, {"device": {"statusLedPin": 22}})
        client.json_request.assert_called_once_with("/api/settings", value={"device": {"statusLedPin": 22}})

    def test_lost_ack_resumes_at_confirmed_offset(self):
        device = FakeDevice()
        with patch("elma_flasher.time.sleep"):
            DesignerServer(None).upload_elma_ota(DesignerJob(), device, b"x" * 25000, "test.bin")
        self.assertEqual(device.offset, 25000)
        self.assertEqual(device.starts, 1)
        self.assertTrue(device.finished)

    def test_device_reboot_restarts_inactive_partition(self):
        device = FakeDevice(reboot=True)
        with patch("elma_flasher.time.sleep"):
            DesignerServer(None).upload_elma_ota(DesignerJob(), device, b"x" * 25000, "test.bin")
        self.assertEqual(device.offset, 25000)
        self.assertEqual(device.starts, 2)
        self.assertTrue(device.finished)

    def test_minimal_profiles_are_separate(self):
        server = DesignerServer(None)
        for chip in ("esp32", "esp32s3", "esp32c3"):
            job = DesignerJob()
            self.assertEqual(server.resolve_profile(chip, {}, {}, job, "minimal"), chip + "_ota_bridge")
            self.assertIn("ota-bridge", server.generated_firmware_name(chip, {}, "minimal"))

    def test_unchanged_retry_reuses_binary_and_settings_change_rebuilds(self):
        with tempfile.TemporaryDirectory() as folder:
            project = pathlib.Path(folder)
            build = project / ".pio" / "build" / "esp32c3_ota_bridge"
            build.mkdir(parents=True)
            application = bytearray(128)
            application[0] = 0xE9
            application[12] = 5
            (build / "firmware.bin").write_bytes(application)
            server = DesignerServer(None)
            server.project_root = lambda: project
            server.portable_home = lambda: project
            server.generated_firmware_directory = lambda: project
            server.compiler_command = lambda: ["fake-compiler"]
            process = Mock(stdout=[])
            process.wait.return_value = 0
            payload = {"compileOnly": True, "transport": "ip", "chip": "esp32c3", "firmwareMode": "minimal", "settings": {}}
            with patch("elma_flasher.subprocess.Popen", return_value=process) as compiler:
                first, retry, changed = DesignerJob(), DesignerJob(), DesignerJob()
                server.run_job(first, payload)
                server.run_job(retry, payload)
                self.assertEqual(first.state, "complete", first.error)
                self.assertEqual(retry.state, "complete", retry.error)
                self.assertEqual(compiler.call_count, 1)
                server.run_job(changed, {**payload, "settings": {"wifi": {"ssid": "changed"}}})
                self.assertEqual(changed.state, "complete", changed.error)
                self.assertEqual(compiler.call_count, 2)


if __name__ == "__main__":
    unittest.main()
