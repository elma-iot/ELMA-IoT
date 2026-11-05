import importlib.util, json, pathlib, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('runtime_project_defaults',ROOT/'scripts/project_defaults.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class RuntimeDefaultTests(unittest.TestCase):
 def test_new_compilation_rebinds_imported_runtime_identity(self):
  for packed in [False,True]:
   with self.subTest(packed=packed),tempfile.TemporaryDirectory() as directory:
    root=pathlib.Path(directory);(root/'include').mkdir()
    helpers={'control:0':{'DIN':'3','_runtimeId':'old-image-id','_runtimeProfile':'ws2812-neopixel-led-strip'}}
    project={'ui':{'peripheralHelperBindings':json.dumps(helpers) if packed else helpers},'compiledLogic':{'schemaVersion':1,'nodes':[],'connections':[],'devices':[{'binding':{'kind':'peripheral','group':'control','index':0},'peripheral':{'id':'compiled-project-id','profile':'ws2812-neopixel-led-strip'}}]}}
    result=module.generate_defaults(root,json.dumps(project));actual=result['ui']['peripheralHelperBindings']
    self.assertEqual(isinstance(actual,str),packed)
    if packed:actual=json.loads(actual)
    self.assertEqual(actual['control:0']['_runtimeId'],'compiled-project-id')
    self.assertEqual(actual['control:0']['DIN'],'3')
