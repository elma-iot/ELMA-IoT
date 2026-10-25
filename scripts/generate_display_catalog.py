"""Emit the small C++ controller table from the UI's authoritative catalog."""
import json
from pathlib import Path

def generate(root):
    entries=json.loads((root/'web/modules/spi-display-catalog.json').read_text(encoding='utf8'))
    rows=[]
    for e in entries:
        rows.append('    {'+','.join([json.dumps(e['id']),json.dumps(e['driver']),str(e['width']),str(e['height']),str(e.get('variant',0)),str(e.get('round',False)).lower(),str(e.get('monochrome',False)).lower()])+'},')
    source='''#pragma once
// Generated from web/modules/spi-display-catalog.json. Do not hand-edit.
#include <stdint.h>
#include <cstring>
namespace SpiDisplays {
struct Controller {const char* id;const char* driver;uint16_t width,height;uint8_t variant;bool round,monochrome;};
inline constexpr Controller controllers[]={
'''+ '\n'.join(rows)+'''
};
inline const Controller* controller(const char* id){for(const auto& item:controllers)if(!strcmp(id,item.id))return &item;return nullptr;}
}
'''
    (root/'include/spi_display_catalog.h').write_text(source,encoding='utf8')

if __name__=='__main__':generate(Path(__file__).resolve().parents[1])
