"""Exercise live status polling in the actual Qt web UI using isolated settings."""
import pathlib
import tempfile
from unittest.mock import patch

import board_gpio_smoke_test as smoke
from elma_flasher import DesignerServer

SCRIPT = r"""
(async () => {
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  const check = (ok, label) => { if (!ok) throw new Error(label); };
  try {
    await sleep(1500);
    // Exercise the same polling path as an actual device, with deterministic
    // status responses from the isolated Designer backend.
    const originalFetch = window.fetch.bind(window);
    const status = await (await originalFetch('/api/status')).json();
    let polls = 0;
    window.fetch = (url, options) => {
      if (String(url).startsWith('/api/status')) {
        polls++;
        return Promise.resolve(new Response(JSON.stringify(status), {headers:{'Content-Type':'application/json'}}));
      }
      return originalFetch(url, options);
    };
    document.body.classList.remove('local-builder-mode');
    document.querySelector('[data-tab="wifi"]').click();
    await sleep(2400);
    const diagram = document.getElementById('peripheralDiagramItems');
    check(diagram, 'diagram missing');
    let mutations = 0;
    const observer = new MutationObserver(records => { mutations += records.length; });
    observer.observe(diagram, {childList:true,subtree:true});
    const before = polls;
    await sleep(4400);
    observer.disconnect();
    check(polls >= before + 2, 'live status polling did not run');
    check(mutations === 0, 'hidden diagram rebuilt during polling: ' + mutations);
    document.querySelector('[data-tab="gpio"]').click();
    const select = document.getElementById('statusLedPin');
    select.focus();
    const originalOption = select.options[0];
    const originalValue = select.value;
    await sleep(2400);
    check(document.activeElement === select, 'status polling stole focus');
    check(select.options[0] === originalOption, 'status polling rebuilt focused options');
    check(select.value === originalValue, 'status polling changed the selected pin');
    window.__boardGpioTest = {ok:true,polls,hiddenDiagramMutations:mutations};
  } catch(error) { window.__boardGpioTest = {ok:false,error:String(error.stack || error)}; }
})();
"""

def create_test_server():
    server = DesignerServer(None)
    server.settings['ui'].update(gpioBoardAutodetect=False, gpioBoardSelection='wemos-lolin32-mini')
    server.settings['ui']['peripheralProfiles'].update(
        audioProfiles=['pcm5102-i2s-dac'], displayProfiles=['i2c-oled'])
    server.settings['audio'].update(enabled=True, doutPin=25, wsPin=26, bclkPin=27)
    server.settings['oled'].update(enabled=True, sdaPin=23, sclPin=19)
    return server


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as folder:
        with patch.object(DesignerServer, 'portable_home', return_value=pathlib.Path(folder)):
            smoke.SCRIPT = SCRIPT
            raise SystemExit(smoke.run_board_gpio_smoke_test(create_test_server()))
