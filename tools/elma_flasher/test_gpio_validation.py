import unittest
from gpio_validation import validate_gpio_settings, valid_pins

class PinValidationTests(unittest.TestCase):
    def test_legacy_lolin_audio_and_status(self):
        validate_gpio_settings({'device':{'statusLedPin':22}, 'audio':{'bclkPin':27,'wsPin':26,'doutPin':25},'oled':{'enabled':True,'sdaPin':23,'sclPin':19,'resetPin':-1}}, 'esp32','wemos-lolin32-mini')
    def test_invalid_saved_led_and_flash_audio_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'Status LED.*GPIO8'):
            validate_gpio_settings({'device':{'statusLedPin':8}, 'audio':{'bclkPin':10}},'esp32','wemos-lolin32-mini')
    def test_helpers_do_not_bypass_flash_pin_guard(self):
        with self.assertRaisesRegex(ValueError,'GPIO7'):
            validate_gpio_settings({'ui':{'peripheralHelperBindings':'{"audio:1":{"SIG":"7"}}'}},'esp32','wemos-lolin32-mini')
    def test_main_control_flag_is_not_a_pin(self):
        validate_gpio_settings({'ui':{'peripheralHelperBindings':{'input:0':{'SIG':'18','MAIN_CONTROL':'1','SOURCE':'VCC'}}}},'esp32','esp32-wroom')
    def test_removed_peripheral_does_not_block_compile(self):
        validate_gpio_settings({'ui':{'peripheralProfiles':{'audioProfiles':['none']}, 'peripheralHelperBindings':{'audio:0':{'SIG':'7'}}}},'esp32','esp32-wroom')
    def test_s3_flexible_outputs(self):
        validate_gpio_settings({'device':{'statusLedPin':48},'audio':{'bclkPin':14,'wsPin':15,'doutPin':16},'oled':{'enabled':True,'sdaPin':8,'sclPin':9}},'esp32s3','esp32-s3-super-mini')
    def test_input_only_and_psram_are_not_outputs(self):
        self.assertNotIn(36, valid_pins('esp32',True))
        self.assertIn(36, valid_pins('esp32',False))
        self.assertNotIn(16, valid_pins('esp32',True,'esp32-wrover'))
        self.assertNotIn(35, valid_pins('esp32s3',True,'esp32-s3-psram'))
    def test_duplicate_audio_pins(self):
        with self.assertRaisesRegex(ValueError,'different GPIOs'):
            validate_gpio_settings({'audio':{'bclkPin':27,'wsPin':27,'doutPin':25}},'esp32','esp32-wroom')

if __name__ == '__main__': unittest.main()
