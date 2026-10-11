const table = Uint32Array.from({length:256},(_,n)=>{for(let b=0;b<8;b++)n=(n>>>1)^((n&1)?0xedb88320:0);return n>>>0;});
export function crc32Update(crc,bytes){for(const byte of bytes)crc=(crc>>>8)^table[(crc^byte)&255];return crc>>>0;}
export function crc32Hex(crc){return ((crc^0xffffffff)>>>0).toString(16).padStart(8,'0');}
const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
export async function fileCrc32(file,signal,onProgress=()=>{}){let crc=0xffffffff;for(let offset=0;offset<file.size;offset+=262144){if(signal?.aborted)throw new DOMException("Upload cancelled","AbortError");crc=crc32Update(crc,new Uint8Array(await file.slice(offset,offset+262144).arrayBuffer()));onProgress(Math.min(offset+262144,file.size),file.size);}return crc32Hex(crc);}
export async function retryTransfer(work,onRetry=()=>{},attempts=3){let last;for(let attempt=0;attempt<attempts;attempt++){try{return await work(attempt);}catch(error){last=error;if(error.name==='AbortError'||error.permanent||attempt+1===attempts)throw error;onRetry(attempt+2,error);await sleep(1000*2**attempt);}}throw last;}
export async function storageChecksum(target,path,signal,onProgress=()=>{}){
 const deadline=Date.now()+30*60*1000;let id='';
 const cancel=()=>{if(id)fetch('/api/storage/checksum?id='+encodeURIComponent(id),{method:'DELETE'}).catch(()=>{});};
 signal?.addEventListener('abort',cancel,{once:true});
 try{
 while(Date.now()<deadline){
  if(signal?.aborted)throw new DOMException("Upload cancelled","AbortError");
  const query=id?'id='+encodeURIComponent(id):'target='+encodeURIComponent(target)+'&path='+encodeURIComponent(path);
  const response=await fetch('/api/storage/checksum?'+query,{cache:'no-store',signal:signal?AbortSignal.any([signal,AbortSignal.timeout(15000)]):AbortSignal.timeout(15000)});
  if([423,429,503].includes(response.status)){onProgress({waiting:true,status:response.status});await sleep(500);continue;}
  if(!response.ok){const error=new Error('Checksum request failed: HTTP '+response.status);error.permanent=[400,401,403,404].includes(response.status);throw error;}
  const result=await response.json();if(result.error)throw new Error(result.error);if(typeof result.complete!=='boolean'||typeof result.id!=='string')throw new Error('Invalid checksum response; update firmware and reload the page');onProgress(result);if(result.complete)return result;id=result.id;await sleep(200);
 }
 throw new Error('File verification timed out; retry when storage is idle.');
 }finally{signal?.removeEventListener('abort',cancel);}
}
export async function verifiedStorageDownload(target,path,onProgress=()=>{}){
 const url='/api/storage/file?target='+encodeURIComponent(target)+'&path='+encodeURIComponent(path)+'&download=1';
 if(window.ElmaAndroidDownloads?.download){window.ElmaAndroidDownloads.download(new URL(url,location.href).href);return {native:true};}
 const name=path.split('/').pop();
 // Writable streams use a temporary destination until close; failed attempts abort it.
 let handle=null;
 if(window.showSaveFilePicker)try{handle=await window.showSaveFilePicker({suggestedName:name});}catch(error){if(error.name==='AbortError')return {cancelled:true};throw error;}
 let blob;
 await retryTransfer(async()=>{
  const expected=await storageChecksum(target,path);let writer=null,reader=null;
  try{
   writer=handle?await handle.createWritable():null;
   const response=await fetch(url,{cache:'no-store',signal:AbortSignal.timeout(30*60*1000)});if(!response.ok)throw new Error('Download failed: HTTP '+response.status);
   const length=Number(response.headers.get('Content-Length'));if(length!==expected.sizeBytes)throw new Error('File length changed; retrying download');
   reader=response.body.getReader();let bytes=0,crc=0xffffffff;const chunks=[];
   for(;;){const {value,done}=await reader.read();if(done)break;bytes+=value.length;if(bytes>expected.sizeBytes)throw new Error('Downloaded file exceeds expected length');crc=crc32Update(crc,value);if(writer)await writer.write(value);else chunks.push(value);onProgress(bytes,expected.sizeBytes);}
   if(bytes!==expected.sizeBytes||crc32Hex(crc)!==expected.crc32)throw new Error('Download checksum/length mismatch');
   if(writer){await writer.close();writer=null;}else blob=new Blob(chunks,{type:response.headers.get('Content-Type')||'application/octet-stream'});
  }catch(error){if(reader)await reader.cancel().catch(()=>{});if(writer)await writer.abort().catch(()=>{});throw error;}
 },attempt=>onProgress(0,0,attempt));
 if(blob){const objectUrl=URL.createObjectURL(blob),link=document.createElement('a');link.href=objectUrl;link.download=name;link.click();setTimeout(()=>URL.revokeObjectURL(objectUrl),60000);}
}
