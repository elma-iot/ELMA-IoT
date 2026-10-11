import {createCameraTab} from '../web/modules/camera-tab.js';
import {createMicrophonesTab} from '../web/modules/microphones-tab.js';
import {createBno055Tab} from '../web/modules/bno055-tab.js';
window.commands=[];
let recording=false,video=false;
async function request(url,options={}){if(options.method==='POST'){commands.push({url,...options});if(url==='/api/media/record'){const action=new URLSearchParams(options.body).get('action');recording=action!=='stop';video=action==='video';}return {accepted:true};}
if(url==='/api/bno055')return {ready:true,enabled:true,ageMs:0,mode:'ndof',alignment:[0,0,0],heading:123.4,roll:-5.6,pitch:2.8,quaternion:[.7071,.0123,-.0456,.7041],accel:[.12,-.05,9.78],gyro:[.1,-.3,.2],mag:[23.4,-12.1,35.6],linear:[.02,-.01,.1],gravity:[.01,-.03,9.78],calibration:{system:3,gyro:3,accel:3,mag:3}};
if(url==='/api/camera/status')return {ready:true,psram:true,framesize:7,quality:15};
if(url==='/api/microphones')return {enabled:true,ready:true,gain:1,left:.2,right:.3};
if(url==='/api/media/status')return {sd:true,recording,video,path:recording?'/record/test.wav':''};
if(url.startsWith('/api/storage'))return {storage:{mounted:true},entries:Array.from({length:7},(_,i)=>({name:`mic-${i}.wav`,path:`/record/mic-${i}.wav`}))};return {};}
window.camera=createCameraTab({request});window.mic=createMicrophonesTab({request});window.bno=createBno055Tab({request});const status={firmware:{camera:true,microphones:true},system:{webUiLocked:false}};camera.update(status);mic.update(status);bno.update({ui:{peripheralProfiles:{sensors:['bno055']}}},false);window.ready=true;
