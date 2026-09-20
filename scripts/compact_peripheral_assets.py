"""Replace unused firmware illustrations while preserving their HTTP routes."""
import json
def escape(value):return value.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;")
from pathlib import Path

def load_manifest(root):return json.loads((Path(root)/'scripts/peripheral-svg-manifest.json').read_text(encoding='utf-8'))
def active_mask(manifest,profiles=None):
 paths=sorted(manifest)
 if len(paths)>31:raise ValueError('Peripheral SVG manifest exceeds supported mask capacity')
 if profiles is None:return (1<<len(paths))-1
 if not isinstance(profiles,list) or not all(isinstance(p,str) for p in profiles):raise ValueError('Active peripheral profiles must be a list of canonical group:profile strings')
 return sum(1<<index for index,path in enumerate(paths) if any(profile in profiles for profile in manifest[path]['profiles']))
def block_svg(specification):
 pins=specification['pins'];height=max(100,36+len(pins)*18);title=specification['title'].replace('-',' ').upper()
 parts=[f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 180 {height}" role="img"><title>{escape(title)}</title><rect x="4" y="4" width="172" height="{height-8}" rx="10" fill="#222c39" stroke="#94a3b8"/><text x="90" y="23" text-anchor="middle" fill="#edf2fb" font-family="sans-serif" font-size="9">{escape(title)}</text>']
 for index,pin in enumerate(pins):
  y=44+index*18
  parts.append(f'<circle cx="12" cy="{y-3}" r="3" fill="#438deb"/><text x="24" y="{y}" fill="#edf2fb" font-family="sans-serif" font-size="11">{escape(pin)}</text>')
 return ''.join(parts)+'</svg>'

def default_svg():
 return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 180 100" aria-hidden="true"><rect x="4" y="4" width="172" height="92" rx="10" fill="#222c39" stroke="#94a3b8"/><circle cx="90" cy="50" r="18" fill="none" stroke="#f59e0b" stroke-width="5"/><path d="M90 38v24M78 50h24" stroke="#edf2fb" stroke-width="5"/></svg>'
