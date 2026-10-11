const http=require('node:http'),fs=require('node:fs'),path=require('node:path'),zlib=require('node:zlib'),assert=require('node:assert/strict');
const {chromium}=require('../node_modules/playwright');
const root=path.resolve(__dirname,'../robot-web/package'),messages=[];
const server=http.createServer((req,res)=>{
 const pathname=new URL(req.url,'http://localhost').pathname;
 if(pathname==='/api/robot'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify({board:'miniexco-v2',batteryVoltage:7.5,chargerVoltage:0}));return;}
 if(pathname.startsWith('/api/')){res.statusCode=404;res.end();return;}
 const name=pathname==='/'?'index.html':pathname.slice(1),p=path.join(root,name);
 if(!p.startsWith(root)||!fs.existsSync(p)){res.statusCode=404;res.end();return;}
 const gzip=/gzip/.test(req.headers['accept-encoding']||'')&&fs.existsSync(p+'.gz');const data=fs.readFileSync(gzip?p+'.gz':p);
 res.setHeader('Content-Type',name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':name.endsWith('.html')?'text/html':'application/octet-stream');res.setHeader('Vary','Accept-Encoding');res.setHeader('Content-Length',data.length);if(gzip)res.setHeader('Content-Encoding','gzip');res.end(data);
});
(async()=>{await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));const browser=await chromium.launch({channel:'chrome',headless:true});try{
 const page=await browser.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));await page.routeWebSocket('**/api/robot/ws',ws=>ws.onMessage(message=>messages.push(JSON.parse(message))));
 await page.addInitScript(()=>{const Original=window.WebSocket;window.WebSocket=new Proxy(Original,{construct(target,args){const socket=Reflect.construct(target,args);window.lastRobotSocket=socket;return socket;}});});
 await page.goto('http://127.0.0.1:'+server.address().port);await page.locator('[data-connect]').click();await page.waitForFunction(()=>document.querySelector('[data-connect]').textContent==='Disable controls');
 await page.locator('[data-stick]').focus();await page.keyboard.down('w');await page.waitForTimeout(120);assert(messages.some(x=>x.left===150&&x.right===150));await page.keyboard.up('w');await page.waitForTimeout(100);assert.equal(messages.at(-1).left,0);
 await page.keyboard.down('r');await page.waitForTimeout(100);assert(messages.some(x=>x.arm===255));await page.keyboard.up('r');
 await page.keyboard.press('Space');await page.waitForTimeout(100);assert(messages.some(x=>x.stop===true));assert.equal(await page.locator('[data-connect]').textContent(),'Enable controls');
 await page.locator('[data-connect]').click();await page.waitForTimeout(100);await page.evaluate(()=>window.dispatchEvent(new Event('blur')));await page.waitForTimeout(100);assert.equal(await page.locator('[data-connect]').textContent(),'Enable controls');await page.locator('[data-connect]').click();await page.waitForTimeout(100);await page.evaluate(()=>Object.defineProperty(window.lastRobotSocket,'bufferedAmount',{get(){return 2048;},configurable:true}));await page.waitForTimeout(150);assert.equal(await page.locator('[data-connect]').textContent(),'Enable controls');assert.deepEqual(errors,[]);
 await page.screenshot({path:path.resolve(__dirname,'../../.build-tools/miniexco-browser-verified.png'),fullPage:true});console.log('PASS: Chrome rendered precompressed HTML/CSS/JS; leased drive, arm, emergency and focus stops');
 }finally{await browser.close();server.close();}})().catch(error=>{console.error(error);server.close();process.exitCode=1;});
