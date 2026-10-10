"""Generate recolorable LCD masks from the authoritative web tab SVGs.

Build-time only: run with PySide6 installed. Firmware needs no SVG renderer.
"""
from html.parser import HTMLParser
from pathlib import Path
import re
from PySide6.QtCore import QByteArray, Qt
from PySide6.QtGui import QImage, QPainter
from PySide6.QtSvg import QSvgRenderer


class Tabs(HTMLParser):
    def __init__(self):
        super().__init__(); self.tab=None; self.svg=None; self.depth=0; self.icons={}
    def handle_starttag(self, tag, attrs):
        a=dict(attrs)
        if tag=='button': self.tab=a.get('data-tab')
        if tag=='svg' and self.tab: self.svg=[]; self.depth=1
        elif self.svg is not None: self.depth+=1
        if self.svg is not None:
            attributes=' '.join(f'{k}="{v}"' for k,v in attrs if k!='data-security-shackle')
            self.svg.append(f'<{tag} {attributes}>')
    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag,attrs); self.handle_endtag(tag)
    def handle_endtag(self, tag):
        if self.svg is not None:
            self.svg.append(f'</{tag}>'); self.depth-=1
            if not self.depth:
                self.icons[self.tab]=''.join(self.svg); self.svg=None
        if tag=='button': self.tab=None


root=Path(__file__).resolve().parents[1]
parser=Tabs(); parser.feed((root/'web/index.html').read_text(encoding='utf-8'))
names=['gpio','logics','wifi','mqtt','device','oled','hardware','storage-internal','firmware','logs','security','info']
lines=['// Generated from web/index.html by scripts/generate_round_icons.py', '#pragma once', '#include <lvgl.h>', 'namespace RoundIcons {']
for index,name in enumerate(names):
    svg=parser.icons[name].replace('<svg ', '<svg xmlns="http://www.w3.org/2000/svg" fill="none" stroke="white" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" ')
    image=QImage(48,48,QImage.Format.Format_ARGB32); image.fill(Qt.GlobalColor.transparent)
    renderer=QSvgRenderer(QByteArray(svg.encode())); assert renderer.isValid(), name
    painter=QPainter(image); renderer.render(painter); painter.end()
    values=[str(image.pixelColor(x,y).alpha()) for y in range(48) for x in range(48)]
    lines.append(f'static const uint8_t mask{index}[]={{'+','.join(values)+'};')
    lines.append(f'static const lv_img_dsc_t icon{index}={{{{LV_IMG_CF_ALPHA_8BIT,0,0,48,48}},sizeof(mask{index}),mask{index}}};')
lines+=['static const lv_img_dsc_t* const icons[]={'+','.join(f'&icon{i}' for i in range(12))+'};']
wire_source=(root/'web/modules/peripheral-pin-model.js').read_text(encoding='utf-8')
wire_map=re.search(r'const SIGNAL_COLORS = \{(.*?)\};',wire_source,re.S).group(1)
lines+=['struct WireColor {const char* signal;uint32_t color;};','static const WireColor wireColors[]={'+','.join('{"'+name+'",0x'+color+'}' for name,color in re.findall(r"(\w+):'#([0-9a-fA-F]{6})'",wire_map))+'};','}']
(root/'include/round_web_icons.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('Generated 12 LCD icons from the web SVGs (27 KiB of alpha masks).')
