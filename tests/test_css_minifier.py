import ast,re,unittest
from pathlib import Path
source=Path(__file__).resolve().parents[1]/'scripts/asset_embed.py'
node=next(n for n in ast.parse(source.read_text()).body if isinstance(n,ast.FunctionDef) and n.name=='minify_css')
namespace={'re':re};exec(compile(ast.Module(body=[node],type_ignores=[]),str(source),'exec'),namespace)
minify=namespace['minify_css']
class CssMinifierTests(unittest.TestCase):
 def test_descendant_pseudo_selector_survives(self):
  out=minify(':root[data-elma-theme="dark"] :is(.storage-embedded-shell, .storage-file-list) { background: var(--panel); color: var(--ink); }')
  self.assertIn('] :is(',out);self.assertNotIn(']:is(',out)
 def test_nested_media_descendant_and_disabled(self):
  out=minify('@media (prefers-color-scheme: dark) { :root:not([data-elma-theme="light"]) :is(input,select):disabled { color: var(--muted); } }')
  self.assertIn(') :is(input,select):disabled',out)
if __name__=='__main__':unittest.main()
