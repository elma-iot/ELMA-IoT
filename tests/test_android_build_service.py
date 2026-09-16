import sys, pathlib, tempfile, json, urllib.request, urllib.error, ssl, unittest, types, zipfile, io, hashlib
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/elma_flasher'))
from android_build_server import AndroidBuildService, make_bundle
class ServiceTest(unittest.TestCase):
 def test_service(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=pathlib.Path(tmp); build=root/'.pio/build/test';build.mkdir(parents=True)
   for name in ('bootloader.bin','partitions.bin','firmware.bin'): (build/name).write_bytes(b'example')
   class Backend:
    def create_job(self,payload):
     self.payload=payload
     return types.SimpleNamespace(id='test-job',state='complete',profile='test',firmware_file=str(build/'firmware.bin'),public=lambda:{'state':'complete','firmwareFile':'private'})
    def project_root(self):return root
    def terminate_job_process(self,job):pass
   backend=Backend();svc=AndroidBuildService(backend,root/'service',{'board':(1,'esp32')},lambda x:x,host='127.0.0.1');svc.start()
   def request(path,body=None,token=None):
    req=urllib.request.Request('https://127.0.0.1:'+str(svc.httpd.server_port)+path,data=json.dumps(body).encode() if body is not None else None,headers={'Authorization':'Bearer '+(token or svc.token)})
    return urllib.request.urlopen(req,context=ssl._create_unverified_context()).read()
   try:
    self.assertEqual(json.loads(request('/v1/status'))['compiler'],'ELMA PC')
    with self.assertRaises(urllib.error.HTTPError) as e:request('/v1/status',token='wrong')
    self.assertEqual(e.exception.code,401)
    with self.assertRaises(urllib.error.HTTPError):request('/v1/build',{'settings':{'ui':{'gpioBoardSelection':'unknown'}}})
    request('/v1/build',{'settings':{'ui':{'gpioBoardSelection':'board'}},'outputPath':'evil','transport':'ip'})
    self.assertTrue(backend.payload['compileOnly']);self.assertEqual(backend.payload['transport'],'usb');self.assertNotEqual(backend.payload['outputPath'],'evil')
    self.assertNotIn('firmwareFile',json.loads(request('/v1/jobs/test-job')))
    archive=zipfile.ZipFile(io.BytesIO(request('/v1/jobs/test-job/bundle')));manifest=json.loads(archive.read('manifest.json'))
    for part in manifest['parts']:self.assertEqual(hashlib.sha256(archive.read(part['file'])).hexdigest(),part['sha256'])
    self.assertEqual(len(archive.read('ota-reset.bin')),8192)
   finally:svc.stop()
if __name__=='__main__':unittest.main()
