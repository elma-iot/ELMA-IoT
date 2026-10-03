// Keep wording aligned with Windows logic_connection_tips.py.
const LOOP_TIP="This connection would create a loop without a timed wait. Put Delay, Timer, Debounce or Countdown in every return path, use a positive duration (start with 5 seconds), then reconnect. Increasing a timer elsewhere does not fix a path that bypasses it. Cooldown and Repeat alone do not provide a timed wait.";
const TYPE_TIP="Connect an enabled output to an input of a compatible type on another element. A data value is not an execution pulse; use a condition and Rising Edge to trigger an action. For disabled GPIO ports, check the selected board, pin and mode.";
const INPUT_TIP="A data input accepts one source. Remove or move its existing wire, then reconnect. Execution inputs can accept different triggers, but the same wire cannot be added twice.";
const INTERVAL_TIP="Connect the source value to Sampling Interval.Value, then its output to Save Data.Value or Transfer to Plotter.Value. Use Delay for a timed execution flow.";
const RESET_TIP="Reset restores the countdown duration and continues only while it is running. After it finishes, connect the return pulse to Start to begin another countdown. Keep Reset for extending an active timeout.";
const DATA_TIP="Circular data dependencies cannot be evaluated. Break the data feedback path and read from an independent source. A Delay or Cooldown does not resolve a circular data dependency.";
export {RESET_TIP};
export function connectionTip(error,data=false){
 const text=String(error);
 if(text.includes('Immediate circular'))return data?DATA_TIP:LOOP_TIP;
 if(text.includes('already connected'))return INPUT_TIP;
 if(text.includes('Sampling Interval output'))return INTERVAL_TIP;
 return TYPE_TIP;
}
export function connectionPopup(title,message,detail=''){
 const tr=value=>window.ElmaI18n?.t?.(value)||window.ElmaFirmwareI18n?.t?.(value)||value;
 document.getElementById('logic-connection-tip')?.remove();
 const dialog=document.createElement('dialog');dialog.id='logic-connection-tip';dialog.setAttribute('aria-labelledby','logic-connection-tip-title');
 dialog.style.cssText='position:fixed;top:15%;max-width:min(560px,90vw);box-sizing:border-box;z-index:10000;padding:20px;border:1px solid #687b91;border-radius:12px;background:#202b37;color:#fff;box-shadow:0 8px 30px #0008';
 const heading=document.createElement('h3');heading.id='logic-connection-tip-title';heading.textContent=tr(title);
 const reason=document.createElement('p');reason.textContent=tr(message);const advice=document.createElement('p');advice.textContent=tr(detail);
 const close=document.createElement('button');close.type='button';close.textContent=tr('OK');close.onclick=()=>dialog.close();dialog.onclose=()=>dialog.remove();
 dialog.append(heading,reason,advice,close);document.body.append(dialog);dialog.show();close.focus();
 dialog.onkeydown=e=>{if(e.key==='Escape'){e.preventDefault();dialog.close();}};
}
