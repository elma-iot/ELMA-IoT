"""Generate compact indexed LCD frames from the Android startup vector paths.
Usage: python scripts/panel_boot_assets.py ../Android/app/src/main/java/com/elmaiot/app/ElmaStartupView.java
Requires Pillow only on the development host; firmware reads flash-resident masks.
"""
from pathlib import Path
import re,sys,math
from PIL import Image,ImageDraw,ImageChops
source=Path(sys.argv[1]).read_text(encoding='utf-8');size=128;scale=size/1254
names=['GREEN_APPLE','RED_ACCENT','RED_SEEDS','GREEN_TEXT','BLUE_TEXT','BLUE_SWOOSH']
colors=[(89,191,39),(236,28,36),(236,28,36),(89,191,39),(8,121,209),(8,121,209)]
def points(name):
 text=re.search(r' '+name+r' = "([^"]+)"',source)[1]
 return [[(float(x)*scale,float(y)*scale) for x,y in re.findall(r'[ML] ([\d.]+) ([\d.]+)',part)] for part in text.split('Z') if part.strip()]
def mask(polys):
 result=Image.new('1',(size,size))
 for poly in polys:
  part=Image.new('1',(size,size));ImageDraw.Draw(part).polygon(poly,fill=1);result=ImageChops.logical_xor(result,part)
 return result
masks=[mask(points(n)) for n in names]
frames=[]
def pack(im,color):
 # Four palette entries (transparent, color, white spark, transparent).
 data=bytearray([0,0,0,0,*color[::-1],255,255,255,255,255,0,0,0,0])
 px=list(im.getdata())
 for i in range(0,len(px),4):data.append(sum((px[i+j]&3)<<(6-2*j) for j in range(4)))
 return data
for im,col in zip(masks[:5],colors[:5]):frames.append(pack(im.convert('L').point(lambda x:1 if x else 0),col))
poly=points('BLUE_SWOOSH')[0];poly=poly+[poly[0]]
lengths=[0]
for a,b in zip(poly,poly[1:]):lengths.append(lengths[-1]+math.dist(a,b))
for step in range(21):
 progress=step/20;distance=lengths[-1]*(1-progress);trail=[]
 for i in range(len(poly)-1):
  if lengths[i+1]>=distance:
   t=max(0,(distance-lengths[i])/(lengths[i+1]-lengths[i]));trail=[(poly[i][0]+(poly[i+1][0]-poly[i][0])*t,poly[i][1]+(poly[i+1][1]-poly[i][1])*t)]+poly[i+1:];break
 reveal=Image.new('1',(size,size))
 if progress>=1:reveal=masks[5]
 elif step and len(trail)>1:
  ImageDraw.Draw(reveal).line(trail,fill=1,width=10);reveal=ImageChops.logical_and(reveal,masks[5])
 im=reveal.convert('L').point(lambda x:1 if x else 0)
 if 0<step<20:
  x,y=trail[0];ImageDraw.Draw(im).ellipse((x-1,y-1,x+1,y+1),fill=2)
 frames.append(pack(im,colors[5]))
out=Path(__file__).resolve().parents[1]/'include/panel_boot_layers.h'
with out.open('w',encoding='utf-8') as f:
 f.write('// Generated from Android ElmaStartupView vector paths. Do not edit.\n#pragma once\n#include <stdint.h>\nstatic const uint8_t elmaBootLayers[][4112] = {\n')
 for frame in frames:
  f.write('{\n');f.write(',\n'.join(','.join(map(str,frame[i:i+64])) for i in range(0,len(frame),64)));f.write('\n},\n')
 f.write('};\n')
print('Generated',len(frames),'frames;',len(frames)*4112,'flash bytes; no frame RAM allocation')
