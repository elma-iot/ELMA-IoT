"""Regression cases for library pruning with the pinned audio archive."""
import importlib.util
import itertools
import json
import os
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('fit_features', Path(__file__).resolve().parents[1]/'scripts/fit_features.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)

class Environment(dict):
    def __init__(self):
        super().__init__(PIOENV='esp32s3_notifier_hacs')
        self.ignored = ['lvgl']
        self.defines = []
        self.flags = []
    def Append(self, CPPDEFINES): self.defines.extend(CPPDEFINES)
    def GetProjectConfig(self): return self
    def GetProjectOption(self, name, default): return self.ignored if name=='lib_ignore' else self.flags
    def set(self, section, name, value): self.ignored = value

class FitFeatureTests(unittest.TestCase):
    def test_noaudio_profile_prunes_inherited_decoder_and_optional_sd(self):
        for flags in (['-Os', '-DAPP_DISABLE_AUDIO=1'], '-Os -D APP_DISABLE_AUDIO=1'):
            for excluded in ([], ['sd']):
                env=Environment();env.flags=flags
                with patch.dict(os.environ,ELMA_EXCLUDED_FEATURES=json.dumps(excluded)):
                    policy.apply(env)
                self.assertIn('ESP32-audioI2S-master',env.ignored)
                self.assertEqual('SD' in env.ignored,'sd' in excluded)

    def test_similarly_named_flags_do_not_disable_audio(self):
        env=Environment();env.flags=['-DAPP_DISABLE_AUDIO_DIAGNOSTICS=1']
        with patch.dict(os.environ,ELMA_EXCLUDED_FEATURES='["sd"]'):policy.apply(env)
        self.assertNotIn('SD',env.ignored)
        self.assertNotIn('ESP32-audioI2S-master',env.ignored)

    def test_all_feature_combinations_preserve_audio_dependencies(self):
        for size in range(4):
            for excluded in itertools.combinations(('audio','display','sd'), size):
                with self.subTest(excluded=excluded):
                    env=Environment()
                    with patch.dict(os.environ, ELMA_EXCLUDED_FEATURES=json.dumps(excluded)):
                        policy.apply(env)
                    self.assertIn('lvgl',env.ignored)
                    self.assertEqual(('APP_DISABLE_SD',1) in env.defines,'sd' in excluded)
                    for name in ('ESP32-audioI2S','ESP32-audioI2S-master'):
                        self.assertEqual(name in env.ignored,'audio' in excluded)
                    for name in ('SD','SD_MMC'):
                        self.assertEqual(name in env.ignored,'audio' in excluded and 'sd' in excluded)

    def test_invalid_policy_does_not_mutate_build(self):
        env=Environment()
        with patch.dict(os.environ, ELMA_EXCLUDED_FEATURES='["wifi"]'):
            with self.assertRaises(ValueError): policy.apply(env)
        self.assertEqual(env.ignored,['lvgl'])
        self.assertEqual(env.defines,[])

if __name__=='__main__': unittest.main()
