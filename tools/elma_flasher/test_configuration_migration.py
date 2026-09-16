"""Regression tests for portable configuration migration."""

import json
import pathlib
import tempfile
import unittest
from unittest.mock import patch

from elma_flasher import DesignerServer


class ConfigurationMigrationTests(unittest.TestCase):
    def test_manual_legacy_open_restores_profiles_and_reopens_on_startup(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            document = root / "My device.json"
            document.write_text(json.dumps({
                "audio": {"doutPin": 25, "wsPin": 26, "bclkPin": 27},
                "oled": {"sdaPin": 23, "sclPin": 19},
                "battery": {"adcPin": 36},
            }), encoding="utf-8")
            with patch.object(DesignerServer, "portable_home", return_value=root):
                server = DesignerServer(None)
                server.open_configuration(document)
                profiles = server.settings["ui"]["peripheralProfiles"]
                self.assertEqual(profiles["audioProfiles"], ["pcm5102-i2s-dac"])
                self.assertEqual(profiles["displayProfiles"], ["i2c-oled"])
                server.save_designer_settings()
                reopened = DesignerServer(None)
                self.assertEqual(reopened.active_settings_path, document)
                self.assertEqual(reopened.settings, server.settings)
                self.assertFalse(reopened.configuration_dirty())

    def test_folder_only_state_recovers_named_document(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            document = root / "ELMA-Device.config.json"
            document.write_text(json.dumps({"device": {"statusLedPin": 22}}), encoding="utf-8")
            (root / "ELMA-Flasher.state.json").write_text(json.dumps({
                "lastConfigurationDirectory": str(root),
            }), encoding="utf-8")
            with patch.object(DesignerServer, "portable_home", return_value=root):
                server = DesignerServer(None)
                self.assertEqual(server.active_settings_path, document)
                self.assertEqual(server.settings["device"]["statusLedPin"], 22)

    def test_explicit_profiles_are_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            document = root / "custom.json"
            document.write_text(json.dumps({"ui": {"peripheralProfiles": {
                "audioProfiles": ["none"], "displayProfiles": ["none"],
            }}}), encoding="utf-8")
            with patch.object(DesignerServer, "portable_home", return_value=root):
                server = DesignerServer(None)
                server.open_configuration(document)
                self.assertEqual(server.settings["ui"]["peripheralProfiles"]["audioProfiles"], ["none"])

    def test_previous_combined_state_is_migrated_to_named_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            old_home = root / "v0.1.42"
            new_home = root / "v0.1.43"
            old_home.mkdir()
            new_home.mkdir()
            old_state = {
                "device": {"statusLedPin": 22, "lowBatterySleepEnabled": False},
                "audio": {"wsPin": 26, "bclkPin": 27, "doutPin": 25},
                "battery": {"adcPin": 36},
                "oled": {"sdaPin": 23, "sclPin": 19},
                "lastConfigurationDirectory": str(old_home),
            }
            (old_home / "ELMA-Flasher.state.json").write_text(json.dumps(old_state), encoding="utf-8")
            (new_home / "ELMA-Flasher.state.json").write_text(
                json.dumps({"lastConfigurationDirectory": str(old_home)}), encoding="utf-8"
            )

            with patch.object(DesignerServer, "portable_home", return_value=new_home):
                server = DesignerServer(None)

            self.assertEqual(server.settings["audio"]["wsPin"], 26)
            self.assertEqual(server.settings["battery"]["adcPin"], 36)
            self.assertEqual(server.settings["oled"]["sdaPin"], 23)
            self.assertTrue(server.settings["oled"]["enabled"])
            self.assertFalse(server.settings["device"]["lowBatterySleepEnabled"])
            self.assertEqual(server.settings["ui"]["gpioBoardSelection"], "wemos-lolin32-mini")
            self.assertEqual(server.settings["ui"]["peripheralProfiles"]["audioProfiles"], ["pcm5102-i2s-dac"])
            self.assertEqual(server.settings["ui"]["peripheralProfiles"]["displayProfiles"], ["i2c-oled"])
            self.assertEqual(server.settings["ui"]["peripheralProfiles"]["inputs"], ["none"])
            self.assertEqual(server.migrated_configuration_source, old_home / "ELMA-Flasher.state.json")
            saved = json.loads((new_home / "ELMA-Flasher.config.json").read_text(encoding="utf-8"))
            self.assertNotIn("lastConfigurationDirectory", saved)


if __name__ == "__main__":
    unittest.main()
