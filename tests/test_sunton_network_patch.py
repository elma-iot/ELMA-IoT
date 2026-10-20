"""Verify repeated builds and migration do not duplicate low-memory guards."""
from pathlib import Path
import runpy
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class NetworkPatchTests(unittest.TestCase):
 def test_fresh_migration_and_idempotence(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);base=root/'board'
   samples={
    'AsyncTCP/src/AsyncTCP.h':'#define CONFIG_ASYNC_TCP_STACK_SIZE 8192 * 2',
    'ESPAsyncWebServer/src/WebRequest.cpp':'void AsyncWebServerRequest::_onData(void *buf, size_t len) {\n}',
    'ESPAsyncWebServer/src/WebServer.cpp':'      AsyncWebServerRequest *r = new AsyncWebServerRequest((AsyncWebServer *)s, c);',
    'ESPAsyncWebServer/src/WebResponses.cpp':'  _ackedLength += len;\n  size_t space = request->client()->space();',
   }
   for name,text in samples.items():
    path=base/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
   class Env:
    def subst(self,key):return str(root) if key=='$PROJECT_LIBDEPS_DIR' else 'board'
   def run():runpy.run_path(str(ROOT/'scripts/patch_async_tcp_stack.py'),init_globals={'Import':lambda _:None,'env':Env()})
   run();expected={name:(base/name).read_text() for name in samples}
   times={name:(base/name).stat().st_mtime_ns for name in samples};run()
   self.assertEqual(times,{name:(base/name).stat().st_mtime_ns for name in samples})
   self.assertIn('CONFIG_ASYNC_TCP_STACK_SIZE 8192\n',expected['AsyncTCP/src/AsyncTCP.h'])
   self.assertIn('< 6144',expected['ESPAsyncWebServer/src/WebRequest.cpp'])
   self.assertIn('< 8192',expected['ESPAsyncWebServer/src/WebServer.cpp'])
   migrations={
    'AsyncTCP/src/AsyncTCP.h': [('STACK_SIZE 8192\n','STACK_SIZE 12288\n')],
    'ESPAsyncWebServer/src/WebRequest.cpp':[('< 6144','< 12288'),('< 1024))','< 2048))')],
    'ESPAsyncWebServer/src/WebServer.cpp':[('< 8192','< 16384'),('< 2048)','< 4096)')],
   }
   for name,pairs in migrations.items():
    text=(base/name).read_text()
    for a,b in pairs:text=text.replace(a,b)
    (base/name).write_text(text)
   run();self.assertEqual(expected,{name:(base/name).read_text() for name in samples})
if __name__=='__main__':unittest.main()
