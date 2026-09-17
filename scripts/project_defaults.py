"""Embed settings and a separate compact, validated runtime logic program."""
import json,hashlib

def generate_defaults(root,raw):
 data=json.loads(raw)
 if not isinstance(data,dict):raise ValueError('Project defaults must be an object')
 editor=data.pop('windowsLogic',None)
 if isinstance(editor,dict) and editor.get('nodes') and 'compiledLogic' not in data:raise ValueError('Nonempty Logics graph was not compiled; use the updated Windows compiler before generating firmware')
 logic=data.pop('compiledLogic',{'schemaVersion':1,'nodes':[],'connections':[]})
 if not isinstance(logic,dict) or logic.get('schemaVersion')!=1 or not isinstance(logic.get('nodes'),list) or not isinstance(logic.get('connections'),list):raise ValueError('Invalid compiled Logics')
 logic_payload=json.dumps(logic,separators=(',',':'),ensure_ascii=True,allow_nan=False)
 if len(logic_payload)>32768 or len(logic['nodes'])>64 or len(logic['connections'])>128:raise ValueError('Compiled Logics exceeds runtime limits')
 data.get('ui',{}).pop('logicPeripheralIds',None)
 payload=json.dumps(data,separators=(',',':'),ensure_ascii=True)
 if len(payload)>131072:raise ValueError('Project defaults exceed 128 KiB')
 tag='elma'+hashlib.sha256(payload.encode()).hexdigest()[:10]
 content='#pragma once\n// Generated locally; may contain project credentials. Never commit.\ninline constexpr char ELMA_COMPILED_PROJECT_DEFAULTS[]=R"'+tag+'('+payload+')'+tag+'";\n'
 logic_tag='logic'+hashlib.sha256(logic_payload.encode()).hexdigest()[:10]
 content+='inline constexpr char ELMA_COMPILED_LOGICS[]=R"'+logic_tag+'('+logic_payload+')'+logic_tag+'";\n'
 path=root/'include'/'generated_project_defaults.h'
 if not path.exists() or path.read_text(encoding='utf8')!=content:path.write_text(content,encoding='utf8')
 return data
