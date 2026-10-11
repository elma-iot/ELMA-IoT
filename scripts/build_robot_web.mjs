import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import {build} from 'esbuild';
const root=path.resolve(import.meta.dirname,'..'),out=path.join(root,'robot-web/package');
fs.mkdirSync(out,{recursive:true});
const html=fs.readFileSync(path.join(root,'robot-web/index.html'));
const bundle=await build({entryPoints:[path.join(root,'robot-web/main.js')],bundle:true,minify:true,write:false,format:'esm',target:'chrome100'});
const existing=fs.readFileSync(path.join(root,'web/style.css'),'utf8');
const robotCss=existing.slice(existing.lastIndexOf('.robot-controls{'));
const css=`*{box-sizing:border-box}body{margin:0;background:#17212c;color:#e8f0fa;font:16px system-ui;padding:16px}header{display:flex;gap:16px;align-items:center;margin-bottom:20px}header img{width:72px}header a{margin-left:auto}button,input,a{font:inherit}button{padding:12px;background:#304a64;border:1px solid #607d99;border-radius:9px;color:inherit;cursor:pointer}a{color:#6bd8ff}input{max-width:220px}.camera-viewport{position:relative}.camera-media-overlay{position:absolute;right:10px;top:10px;display:flex;flex-direction:column;gap:8px}.media-icon{width:44px;height:44px;border-radius:50%;padding:8px;background:#23364ddd}.media-icon svg{width:100%;height:100%;fill:none;stroke:currentColor;stroke-width:2}.media-icon[aria-pressed=true]{box-shadow:0 0 15px #00dcb7;color:#00dcb7}.media-icon[data-media-kind=rec][aria-pressed=true]{background:#f2253b;box-shadow:0 0 20px #ff334e}.media-icon[data-media-kind=rec][aria-pressed=true] circle{fill:currentColor}${robotCss}`;
const entries=[['index.html',html],['robot.js',bundle.outputFiles[0].contents],['robot.css',Buffer.from(css)],['rover.webp',fs.readFileSync(path.join(root,'robot-web/rover.webp'))]];
const files=[];
function save(file,destination,data){fs.writeFileSync(path.join(out,file),data);files.push({file,destination,size:data.length,sha256:crypto.createHash('sha256').update(data).digest('hex')});}
for(const [name,input] of entries){save(name,'/web/miniexco-v2/'+name,Buffer.from(input));if(/\.(html|css|js)$/.test(name))save(name+'.gz','/web/miniexco-v2/'+name+'.gz',zlib.gzipSync(input,{level:9}));}
save('miniexco.glb','/3d/miniexco.glb',fs.readFileSync(path.join(root,'robot-web/miniexco.glb')));
save('horn.wav','/pcm/horn.wav',fs.readFileSync(path.join(root,'robot-web/horn.wav')));
save('capture.wav','/pcm/capture.wav',fs.readFileSync(path.join(root,'robot-web/capture.wav')));
// The root entry is deliberately last: partial uploads retain the old entry.
save('entry.html','/web/index.html',Buffer.from('<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=/web/miniexco-v2/index.html"><a href="/web/miniexco-v2/index.html">MiniExco controls</a>'));
save('entry.html.gz','/web/index.html.gz',zlib.gzipSync(fs.readFileSync(path.join(out,'entry.html')),{level:9}));
fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify({schemaVersion:1,board:'miniexco-v2',files},null,2)+'\n');
console.log(`MiniExco SD package: ${files.length} files, ${files.reduce((sum,file)=>sum+file.size,0)} bytes`);
