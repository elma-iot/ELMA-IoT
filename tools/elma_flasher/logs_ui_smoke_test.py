"""Verify the real Logs page, safe text, scroll retention, and HTTP copying."""
import pathlib
import tempfile
from unittest.mock import patch
import board_gpio_smoke_test as smoke
from elma_flasher import DesignerServer

SCRIPT = r"""
(async () => {
  const sleep = ms => new Promise(resolve=>setTimeout(resolve,ms));
  const check = (ok,message) => {if(!ok)throw new Error(message);};
  try {
    await sleep(1400);
    const originalFetch=window.fetch.bind(window);
    let polls=0, copied='';
    const sample='=== Boot 00000002 | reset reason 3 ===\n'+Array.from({length:200},(_,i)=>`[debug] Line ${i}: device status`).join('\n')+'\n<img src=x onerror=alert(1)>\n';
    window.fetch=(url,options)=>String(url).startsWith('/api/logs')
      ? Promise.resolve(new Response(JSON.stringify({source:'internal',revision:String(++polls),text:sample,notice:'Current and previous boot, up to 2 KiB each. Checkpointed every 60 seconds.'}),{headers:{'Content-Type':'application/json'}}))
      : originalFetch(url,options);
    Object.defineProperty(navigator,'clipboard',{configurable:true,value:undefined});
    document.execCommand=command=>{if(command==='copy'){copied=document.activeElement.value;return true;}return false;};
    document.querySelector('[data-tab="logs"]').click();
    await sleep(700);
    const terminal=document.getElementById('deviceLogText');
    check(terminal.textContent===sample,'Log text mismatch');
    check(!terminal.querySelector('img'),'Log was interpreted as HTML');
    check(terminal.scrollHeight>terminal.clientHeight,'No vertical overflow');
    terminal.scrollTop=100;
    await sleep(2300);
    check(terminal.scrollTop===100,'Polling moved the reader to the bottom');
    document.getElementById('deviceLogCopy').click();
    await sleep(150);
    check(copied===sample,'HTTP copy did not copy exact text');
    document.querySelector('[data-tab="wifi"]').click();
    const before=polls; await sleep(2300);
    check(polls===before,'Logs kept polling in a hidden tab');
    document.querySelector('[data-tab="logs"]').click();
    await sleep(250);
    terminal.scrollTop=0;
    window.__boardGpioTest={ok:true,polls,copy:true,scrollPreserved:true};
  } catch(error){window.__boardGpioTest={ok:false,error:String(error.stack||error)};}
})();
"""

if __name__=='__main__':
    with tempfile.TemporaryDirectory() as folder:
        with patch.object(DesignerServer,'portable_home',return_value=pathlib.Path(folder)):
            smoke.SCRIPT=SCRIPT
            output=pathlib.Path(__file__).resolve().parents[2]/'.elma-flasher-build/logs-tab-verified.png'
            raise SystemExit(smoke.run_board_gpio_smoke_test(DesignerServer(None),output))
