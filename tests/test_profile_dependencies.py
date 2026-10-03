"""Exercise the actual resolved PlatformIO profiles against pruning policy."""
import contextlib
import importlib.util
import io
import itertools
import json
import os
from pathlib import Path
import unittest
from unittest.mock import patch
from platformio.project.config import ProjectConfig

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fit_features',ROOT/'scripts/fit_features.py')
policy=importlib.util.module_from_spec(spec);spec.loader.exec_module(policy)

class Environment(dict):
    def __init__(self,config,section):
        super().__init__(PIOENV=section[4:]);self.config=config;self.section=section
        self.ignored=list(config.get(section,'lib_ignore',[]));self.defines=[]
    def GetProjectOption(self,key,default):
        return self.ignored if key=='lib_ignore' else self.config.get(self.section,key,default)
    def Append(self,CPPDEFINES):self.defines.extend(CPPDEFINES)
    def GetProjectConfig(self):return self
    def set(self,section,key,value):self.ignored=value

class ProfileDependenciesTests(unittest.TestCase):
    def test_every_profile_and_valid_exclusion_combination(self):
        config=ProjectConfig(str(ROOT/'platformio.ini'));checked=0
        for section in config.sections():
            if not section.startswith('env:'):continue
            if not any('asset_embed.py' in s for s in config.get(section,'extra_scripts',[])):continue
            flags=config.get(section,'build_flags',[])
            builtin_noaudio=any('APP_DISABLE_AUDIO=' in flag for flag in flags)
            for size in range(4):
                for excluded in itertools.combinations(('audio','display','sd'),size):
                    # VIEWE's display is protected by unused_features().
                    if section=='env:viewe_uedx24320028e' and 'display' in excluded:continue
                    with self.subTest(profile=section,excluded=excluded):
                        env=Environment(config,section)
                        with patch.dict(os.environ,ELMA_EXCLUDED_FEATURES=json.dumps(excluded)),contextlib.redirect_stdout(io.StringIO()):policy.apply(env)
                        disabled=builtin_noaudio or 'audio' in excluded
                        self.assertEqual('ESP32-audioI2S-master' in env.ignored,disabled)
                        self.assertEqual('SD' in env.ignored,disabled and 'sd' in excluded)
                        self.assertEqual('SD_MMC' in env.ignored,disabled and 'sd' in excluded)
                        self.assertEqual('Adafruit GFX Library' in env.ignored,'display' in excluded)
                        checked+=1
        self.assertGreater(checked,150)

if __name__=='__main__':unittest.main()
