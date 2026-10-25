"""Subset the clock's existing Montserrat glyphs; preserve their native 4-bit AA."""
from pathlib import Path
import re

def generate(root):
    chars=sorted(set('Updateing.0123456789%'))
    lines=['#pragma once','#include <stdint.h>',
           '// Derived from the vendored LVGL Montserrat-Medium bitmap fonts.',
           '// Copyright (c) 2021 LVGL Kft; MIT licence: lib/lvgl/LICENCE.txt.',
           'namespace UpdateFonts {',
           'struct Glyph {uint8_t character,width,height;int8_t x,y;uint16_t advance,offset;};',
           'struct Font {uint8_t height,baseline,count;const Glyph* glyphs;const uint8_t* bitmap;};']
    for size in [8,10,14,20,28,48]:
        text=(root/f'lib/lvgl/src/font/lv_font_montserrat_{size}.c').read_text(encoding='utf8')
        assert '.bitmap_format = 0' in text and '.bpp = 4' in text
        raw=re.search(r'glyph_bitmap\[\]\s*=\s*\{(.*?)\n\};',text,re.S)[1]
        bitmap=bytes(int(n,16) for n in re.findall(r'0x([0-9a-fA-F]+)',raw))
        raw=re.search(r'glyph_dsc\[\]\s*=\s*\{(.*?)\n\};',text,re.S)[1]
        fields=re.findall(r'\{\.bitmap_index\s*=\s*(\d+),\s*\.adv_w\s*=\s*(\d+),\s*\.box_w\s*=\s*(\d+),\s*\.box_h\s*=\s*(\d+),\s*\.ofs_x\s*=\s*(-?\d+),\s*\.ofs_y\s*=\s*(-?\d+)\}',raw)
        result=bytearray();glyphs=[]
        for char in chars:
            at,advance,w,h,x,y=map(int,fields[ord(char)-31]);length=(w*h+1)//2
            glyphs.append([ord(char),w,h,x,y,advance,len(result)]);result.extend(bitmap[at:at+length])
        height=int(re.search(r'\.line_height\s*=\s*(\d+)',text)[1]);baseline=int(re.search(r'\.base_line\s*=\s*(\d+)',text)[1])
        lines.append(f'inline constexpr uint8_t pixels{size}[]={{'+','.join(map(str,result))+'};')
        lines.append(f'inline constexpr Glyph glyphs{size}[]={{'+','.join('{'+','.join(map(str,g))+'}' for g in glyphs)+'};')
        lines.append(f'inline constexpr Font font{size}={{{height},{baseline},{len(glyphs)},glyphs{size},pixels{size}}};')
    lines.extend(['inline const Glyph* glyph(const Font& font,char c){for(unsigned n=0;n<font.count;++n)if(font.glyphs[n].character==uint8_t(c))return &font.glyphs[n];return nullptr;}','}'])
    (root/'include/update_font_assets.h').write_text('\n'.join(lines)+'\n',encoding='utf8')

if __name__=='__main__':generate(Path(__file__).resolve().parents[1])
