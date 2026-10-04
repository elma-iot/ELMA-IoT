"""ESP8266 flash-resident assets. Never copies ESP32 partitions or other board SVGs."""
Import("env")
from pathlib import Path
import gzip,json,os
root=Path(env['PROJECT_DIR'])
import sys
sys.path.insert(0,str(root/'scripts'))
from led_modules import apply as apply_led_modules
apply_led_modules(env)
board=os.environ.get('ELMA_SELECTED_BOARD_PROFILE') or {'esp32c6_compact':'esp32-c6','esp32c2_compact':'esp32-c2-esp8684','esp8266_1m':'esp8266-esp01s','esp8285_1m':'esp8285-generic'}.get(env['PIOENV'],'esp8266-wemos-d1-mini')
boards=json.loads((root/'scripts/runtime-board-catalog.json').read_text(encoding='utf8'))
info=boards[board];chip=info['chip'];small=chip in ('esp8266','esp8285')
expected={'esp32c6_compact':'esp32c6','esp32c2_compact':'esp32c2'}.get(env['PIOENV'],'esp8266')
if ('esp8266' if small else chip)!=expected:raise RuntimeError('Selected board does not match firmware target')
if chip=='esp32c6' and os.environ.get('ELMA_NATIVE_USB','1')=='1':env.Append(CPPDEFINES=[('ARDUINO_USB_CDC_ON_BOOT',1),('ARDUINO_USB_MODE',1)])
supplied_path=os.environ.get('ELMA_PROJECT_DEFAULTS_FILE')
supplied=json.loads(Path(supplied_path).read_text(encoding='utf8')) if supplied_path else {}
graph=supplied.get('windowsLogic',{})
vcc=small and any(n.get('type')=='mainboard.esp8266.vcc' for n in graph.get('nodes',[]))
if vcc and any(n.get('type')=='hardware.gpio' and n.get('parameters',{}).get('pin')==17 for n in graph.get('nodes',[])):raise RuntimeError('VCC mode conflicts with external A0; disconnect A0 and remove external ADC nodes')
if vcc:env.Append(CPPDEFINES=[('ELMA_ADC_VCC',1)])
allowed=info['capabilities']
pins=sorted(set(allowed['false:input'])|set(allowed['false:output']))
bundle=root/'web/esp8266/app.bundle.js'
if not bundle.exists():raise RuntimeError('Missing prebuilt ESP8266 web bundle; run scripts/build_esp8266_web.mjs with Node before packaging')
items=[('/', 'text/html',root/'web/esp8266/index.html'),('/app.js','text/javascript',bundle),('/logic-editor.css','text/css',root/'web/logic-editor.css'),('/style.css','text/css',root/'web/style.css')]
language=os.environ.get('ELMA_COMPILED_LANGUAGE','en')
locale=root/'web/esp8266/locales'/f'{language}.js'
if not locale.is_file():raise RuntimeError('Missing compact language bundle: '+language)
items.append(('/firmware-i18n.js','text/javascript',locale))
asset=info['asset']['src'].lstrip('/')
items.append(('/board.svg','image/svg+xml',root/'web'/asset))
lines=['#pragma once','#include <Arduino.h>','struct CompactAsset {const char* path;const char* type;const uint8_t* data;size_t size;};']
for i,(route,mime,path) in enumerate(items):
 raw=path.read_bytes()
 if route=='/':
  page=raw.decode('utf8')
  for css in ('style.css','logic-editor.css'):
   page=page.replace('<link rel="stylesheet" href="/'+css+'">','<style>'+(root/'web'/css).read_text(encoding='utf8')+'</style>')
  page=page.replace('<script defer src="/firmware-i18n.js"></script>','<script>'+locale.read_text(encoding='utf8')+'</script>')
  raw=page.encode('utf8')
 data=gzip.compress(raw,compresslevel=9,mtime=0)
 lines.append('static const uint8_t compact_asset_'+str(i)+'[] PROGMEM={'+','.join(map(str,data))+'};')
lines.append('static const CompactAsset ELMA_8266_ASSETS[]={'+','.join('{'+json.dumps(route)+','+json.dumps(mime)+',compact_asset_'+str(i)+',sizeof(compact_asset_'+str(i)+')}' for i,(route,mime,path) in enumerate(items))+'};')
caps=json.loads((root/'web/esp8266/capabilities.json').read_text(encoding='utf8'))
caps['types']=[t for t in caps['types'] if not t.startswith('mainboard.esp8266.') or small]
if not vcc:caps['types']=[t for t in caps['types'] if t!='mainboard.esp8266.vcc']
pin_caps={str(p):{'input':p in allowed['false:input'],'output':p in allowed['false:output'],'analog':p in allowed['false:adc'],'pwm':p in allowed['false:output'],'pull':p in allowed['false:pull-input'],'pullup':p in allowed['false:pull-input'],'pulldown':p==16 if small else p in allowed['false:pull-input']} for p in pins}
if small and info.get('adcPad') and not vcc:pin_caps['17']={'input':False,'output':False,'analog':True,'pwm':False,'pull':False,'pullup':False,'pulldown':False,'label':'A0 (dedicated ADC)'}
for device in caps['devices']:
 if device['type']=='hardware.gpio':device['binding'].update(allowedPins=pin_caps,maxPwmFrequency=1000 if small else 20000)
 if device['type']=='hardware.led':device['binding'].update(pin=info['defaults']['statusLedPin'],ledType=info['defaults']['statusLedType'])
caps['devices'].extend(supplied.get('compactDevices',[]))
caps['types']=list(dict.fromkeys(caps['types']+[d['type'] for d in supplied.get('compactDevices',[])]))
for name,key in [('TYPES','types'),('DEVICES','devices')]:lines.append('static const char ELMA_8266_'+name+'[] PROGMEM='+json.dumps(json.dumps(caps[key],separators=(',',':')))+';')
lines.append('static const char ELMA_8266_BOARD[]='+json.dumps(board)+';')
lines.append('static const char ELMA_COMPACT_CHIP[]='+json.dumps(chip)+';')
lines.append('static constexpr int ELMA_COMPACT_PWM_MAX='+str(1000 if small else 20000)+';')
for name,values in [('GPIO',pins),('LED',sorted(set(pins)|{p for k,p in info['defaults'].items() if k.endswith('Pin') and isinstance(p,int) and p>=0})),('ADC',allowed['false:adc'])]:
 lines.append('inline bool compact'+name+'(int p){return '+('||'.join('p=='+str(p) for p in values) or 'false')+';}')
lines.append('static const char ELMA_COMPACT_BOARD_INFO[] PROGMEM='+json.dumps(json.dumps({'id':board,'name':info['asset'].get('alt',board).removesuffix(' board'),'chip':chip,'pins':sorted(set(pins)|{p for k,p in info['defaults'].items() if k.endswith('Pin') and isinstance(p,int) and p>=0}),'defaults':info['defaults'],'asset':'/board.svg','contacts':json.loads((root/'web/board-pin-contacts.json').read_text(encoding='utf8')).get(board,{})},separators=(',',':')))+';')
defaults={'device':{k:v for k,v in info['defaults'].items() if k!='preserveExternalLed'},'ui':{'gpioBoardSelection':board,'language':os.environ.get('ELMA_COMPILED_LANGUAGE','en')}}
path=os.environ.get('ELMA_PROJECT_DEFAULTS_FILE')
if path:
 supplied=json.loads(Path(path).read_text(encoding='utf8'))
 for section in ('device','wifi'):defaults.setdefault(section,{}).update(supplied.get(section,{}))
 defaults['windowsLogic']=supplied.get('windowsLogic',{})
 defaults['ui'].update({k:v for k,v in supplied.get('ui',{}).items() if k in ('peripheralProfiles','peripheralHelperBindings','logicPeripheralIds','peripheralDiagramPositions')})
lines.append('static const char ELMA_COMPACT_DEFAULTS[] PROGMEM='+json.dumps(json.dumps(defaults,separators=(',',':')))+';')
lines.append('static constexpr bool ELMA_COMPACT_ADC_PAD='+str(bool(info.get('adcPad')) and not vcc).lower()+';')
(root/'include/generated_esp8266_assets.h').write_text('\n'.join(lines),encoding='utf8')

import re
source=(root/'include/logic_catalog.h').read_text(encoding='utf8')
full=json.loads(''.join(re.findall(r'R"elma\((.*?)\)elma"',source,re.S)))
compact={k:v for k,v in full.items() if k in caps['types']}
(root/'include/generated_esp8266_catalog.h').write_text('#pragma once\n#include <Arduino.h>\nstatic const char ESP8266_LOGIC_CATALOG[] PROGMEM='+json.dumps(json.dumps(compact,separators=(',',':')))+';\n#define ELMA_LOGIC_CATALOG FPSTR(ESP8266_LOGIC_CATALOG)\n',encoding='utf8')
