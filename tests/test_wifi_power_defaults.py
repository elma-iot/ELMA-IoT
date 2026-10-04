"""Offline compiled-default and persistence wiring regressions."""
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("wifi_power_defaults", ROOT / "scripts/wifi_power_defaults.py")
power = importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)


class WifiPowerTests(unittest.TestCase):
    def test_defaults_preserve_previous_power(self):
        self.assertEqual(power.power_defines({}), [("APP_DEFAULT_WIFI_STA_TX_DBM", "15.0f"), ("APP_DEFAULT_WIFI_AP_TX_DBM", "15.0f")])

    def test_selected_defaults_are_compiled_separately(self):
        self.assertEqual(power.power_defines({"ELMA_STA_TX_POWER_DBM": "19.5", "ELMA_AP_TX_POWER_DBM": "8"}), [("APP_DEFAULT_WIFI_STA_TX_DBM", "19.5f"), ("APP_DEFAULT_WIFI_AP_TX_DBM", "8.0f")])

    def test_normalize_matches_ui(self):
        for value in (None, "", "bad", float("nan"), float("inf")):
            self.assertEqual(power.normalize_power(value), 15)
        for value, expected in ((-10, 2), (30, 19.5), (10.26, 10.5)):
            self.assertEqual(power.normalize_power(value), expected)

    def test_nvs_json_roundtrip_wiring_and_power_only_no_restart(self):
        settings = (ROOT / "src/settings_manager.cpp").read_text()
        for mode in ("sta", "ap"):
            self.assertIn(f'readFloat("wifi_{mode}_tx", settings.wifi.{mode}TxPowerDbm)', settings)
            # Persistence now removes values equal to the compiled defaults.
            # Check both the selected value and its matching baseline, then the
            # float adapter below; a direct writer call is no longer expected.
            self.assertIn(f'storeFloat("wifi_{mode}_tx", sanitized.wifi.{mode}TxPowerDbm, baseline.wifi.{mode}TxPowerDbm)', settings)
            self.assertIn(f'wifi["{mode}TxPowerDbm"] = settings.wifi.{mode}TxPowerDbm', settings)
            self.assertIn(f'settings.wifi.{mode}TxPowerDbm = wifi["{mode}TxPowerDbm"].as<float>()', settings)
        adapter = settings.split("auto storeFloat =", 1)[1].split("};", 1)[0]
        self.assertIn("value == initial ? removeDefault(key)", adapter)
        self.assertIn("writeFloatIfChanged(key, value)", adapter)
        manager = (ROOT / "src/wifi_manager.cpp").read_text()
        restart = manager.split("bool wifiRestartRequired", 1)[1].split("}  // namespace", 1)[0]
        self.assertNotIn("TxPower", restart)
        self.assertIn("if (!needsRestart) {\n        applyRadioTxPower();", manager)


if __name__ == "__main__":
    unittest.main()
