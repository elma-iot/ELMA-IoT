"""Render the real LVGL theme on Windows with MSVC; no attached LCD required.

Run: python tests/run_panel_theme.py [output-directory]
The output includes 240/320-pixel PPM images and contrast/frame assertions.
"""
from pathlib import Path
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
output=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'.pio/panel-theme-test'
output.mkdir(parents=True,exist_ok=True)
vc=next((p for p in (
    Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'),
    Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')) if p.exists()),None)
if vc is None:raise RuntimeError('Visual Studio C++ build tools are required')
(output/'lv_conf.h').write_text('#define LV_CONF_H\n#define LV_COLOR_DEPTH 16\n#define LV_COLOR_16_SWAP 1\n#define LV_MEM_SIZE (4*1024*1024)\n#define LV_FONT_MONTSERRAT_14 1\n#define LV_FONT_MONTSERRAT_18 1\n#define LV_USE_THEME_DEFAULT 0\n#define LV_USE_THEME_BASIC 1\n#define LV_BUILD_EXAMPLES 0\n#define LV_USE_LOG 0\n')
flags=f'/I"{output}" /I"{root / "lib/lvgl"}" /I"{root / "src"}" /I"{root / "include"}" /DLV_CONF_INCLUDE_SIMPLE /DAPP_HAS_ONBOARD_PANEL=1 /D_CRT_SECURE_NO_WARNINGS'
sources=list((root/'lib/lvgl/src').rglob('*.c'))
(output/'c.rsp').write_text('/nologo /TC /std:c11 /O1 /MP8 /c '+flags+'\n'+'\n'.join('"'+str(p)+'"' for p in sources))
objects=' '.join('"'+p.stem+'.obj"' for p in sources)
(output/'build.cmd').write_text(f'call "{vc}" >nul\ncl @c.rsp\nif errorlevel 1 exit /b 1\ncl /nologo /std:c++17 /EHsc /O1 {flags} "{root / "src/panel_theme.cpp"}" "{root / "src/panel_boot_animation.cpp"}" "{root / "tests/panel_theme_render.cpp"}" {objects} /Fe:panel-theme-test.exe\nif errorlevel 1 exit /b 1\npanel-theme-test.exe\n')
subprocess.run(['cmd','/c',str(output/'build.cmd')],cwd=output,check=True)
