"""Package shared Designer assets without desktop executables or user data."""
import ast
import json
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
ANDROID = ROOT.parent / 'Android'
DEST = ANDROID / 'app/src/main/assets/site'
DEST.mkdir(parents=True, exist_ok=True)
esbuild = ROOT/'node_modules/@esbuild/win32-x64/esbuild.exe'
subprocess.run([str(esbuild), str(ROOT/'web/app.js'), '--bundle', '--format=esm', '--minify', f'--outfile={DEST / "app.js"}'], check=True)
for source in (ROOT/'web').rglob('*'):
    if source.is_file() and source.suffix != '.js':
        target=DEST/source.relative_to(ROOT/'web');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target)
for source in (ANDROID/'web').iterdir(): shutil.copyfile(source,DEST/source.name)
subprocess.run([str(esbuild), str(ANDROID/'web/android-build.js'), '--bundle', '--format=esm', '--minify', f'--outfile={DEST / "android-build.js"}'], check=True)
page=(DEST/'index.html').read_text(encoding='utf-8')
page=page.replace('<title>', '<meta name="color-scheme" content="light dark"><title>',1)
page=page.replace('<link rel="stylesheet" href="/style.css">', '<link rel="stylesheet" href="/style.css"><link rel="stylesheet" href="/mobile.css">')
page=page.replace('src="/app.js"','src="/android-bootstrap.js"')
(DEST/'index.html').write_text(page,encoding='utf-8')
# Execute only the literal default-settings function, never load desktop/user state.
tree=ast.parse((ROOT.parent/'Windows/elma_flasher.py').read_text(encoding='utf-8'))
function=next(node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name=='default_designer_settings')
scope={};exec(compile(ast.Module(body=[function],type_ignores=[]),'<default settings>','exec'),scope)
(DEST/'default-settings.json').write_text(json.dumps(scope['default_designer_settings'](),indent=2),encoding='utf-8')
print('Android shared assets prepared:',DEST)

licenses=DEST/'licenses'
licenses.mkdir(exist_ok=True)
for package in ('esptool-js','fflate','@noble/hashes'):
    for license_file in (ROOT/'node_modules'/package).glob('LICENSE*'):
        if license_file.is_file(): shutil.copyfile(license_file,licenses/(package.replace('/','-')+'.txt'))
usb_license=ANDROID/'USB-SERIAL-LICENSE.txt'
if usb_license.exists(): shutil.copyfile(usb_license,licenses/usb_license.name)

for name in ('index.html','build.html','privacy.html'):
    target=DEST/name
    if target.exists():
        page=target.read_text(encoding='utf-8')
        policy="<meta http-equiv=\"Content-Security-Policy\" content=\"script-src 'self'; frame-src 'none'; object-src 'none'; base-uri 'none'\">"
        page=page.replace('<head>','<head>'+policy,1)
        target.write_text(page,encoding='utf-8')
