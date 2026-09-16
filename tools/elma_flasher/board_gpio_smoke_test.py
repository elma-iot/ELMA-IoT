"""Exercise the actual Designer DOM without loading or writing user configs."""
import json

SCRIPT = r"""
(async () => {
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  const check = (condition, label) => { if (!condition) throw new Error(label); };
  const change = (element, value) => { check(element, 'missing control'); element.value = value; element.dispatchEvent(new Event('change', {bubbles:true})); };
  const option = (element, value) => [...element.options].find(item => item.value === value);
  const binding = key => document.querySelector(`[data-peripheral-binding-key="${key}"]`);
  try {
    await sleep(1200);
    const board = document.getElementById('gpioBoardSelector');
    const auto = document.getElementById('gpioBoardAutodetect');
    const target = document.getElementById('localBuilderChip');
    auto.checked = false; auto.dispatchEvent(new Event('change', {bubbles:true}));
    check(!option(board, 'esp32-wrover').disabled, 'manual WROVER disabled');
    check(!option(board, 'esp32-spk-n16r8').disabled, 'manual SPK disabled');
    change(board, 'wemos-lolin32-mini');
    change(document.getElementById('peripheralAudioProfile'), 'pcm5102-i2s-dac');
    change(document.getElementById('peripheralDisplayProfile'), 'i2c-oled');
    await sleep(350);
    check(target.value === 'esp32', 'board did not synchronize target chip');
    const led = document.getElementById('statusLedPin');
    check(led.closest('#tab-gpio') && getComputedStyle(led).display !== 'none', 'status GPIO control hidden');
    check(led.value === '8' && option(led,'8').disabled, 'invalid saved LED assignment was lost or enabled');
    check(document.getElementById('gpioAssignmentWarnings').textContent.includes('GPIO8'), 'hidden assignment warning missing');
    check(!option(led,'48'), 'classic ESP32 offers GPIO48');
    for (const [key, pin] of [['audio.wsPin','26'],['audio.bclkPin','27'],['audio.doutPin','25']]) {
      check(option(binding(key),pin) && !option(binding(key),pin).disabled, `legacy DAC ${pin} unavailable`);
      change(binding(key),pin);
    }
    change(led,'22');
    change(binding('oled.sdaPin'),'23'); change(binding('oled.sclPin'),'19');
    await sleep(1800);
    const saved = await (await fetch('/api/settings')).json();
    check(saved.audio.wsPin === 26 && saved.audio.bclkPin === 27 && saved.audio.doutPin === 25, 'DAC selection not saved');
    check(saved.device.statusLedPin === 22, 'LED reassignment not saved');
    check(binding('audio.bclkPin').value === '27', 'autosave reverted DAC');
    change(board,'esp32-s3-super-mini');
    check(option(led,'48') && !option(led,'48').disabled, 'S3 GPIO48 unavailable');
    check(option(binding('audio.bclkPin'),'15') && !option(binding('audio.bclkPin'),'15').disabled, 'S3 I2S still hard-coded');
    change(led,'48');
    check(option(binding('oled.sdaPin'),'8') && !option(binding('oled.sdaPin'),'8').disabled, 'GPIO8 not freed after LED reassign');
    auto.checked = true; auto.dispatchEvent(new Event('change',{bubbles:true}));
    await sleep(300);
    change(target,'esp32s3');
    check(!option(board,'esp32-spk-n16r8').disabled, 'SPK filtered from S3');
    change(target,'auto');
    check(!option(board,'esp32-wrover').disabled, 'Auto did not clear stale filter');
    auto.checked = false; auto.dispatchEvent(new Event('change',{bubbles:true}));
    change(board,'wemos-lolin32-mini');
    change(led,'22');
    window.__boardGpioTest = {ok:true};
  } catch(error) { window.__boardGpioTest = {ok:false,error:String(error.stack || error)}; }
})();
"""

def run_board_gpio_smoke_test(server, screenshot_path=None):
    from PySide6.QtCore import QTimer, QUrl
    from PySide6.QtWidgets import QApplication
    from PySide6.QtWebEngineWidgets import QWebEngineView
    app = QApplication.instance() or QApplication([])
    view = QWebEngineView()
    view.resize(1200, 1000)
    result = {'ok':False, 'error':'Timed out'}
    timer = QTimer()
    timer.setInterval(250)

    def receive(value):
        if not value:
            return
        result.clear()
        result.update(json.loads(value))
        if screenshot_path:
            view.grab().save(str(screenshot_path))
        timer.stop()
        app.quit()

    timer.timeout.connect(lambda: view.page().runJavaScript('JSON.stringify(window.__boardGpioTest || null)', lambda value: receive(value) if value and value != 'null' else None))
    view.loadFinished.connect(lambda ok: view.page().runJavaScript(SCRIPT) if ok else app.quit())
    view.setUrl(QUrl(server.start() + '?elmaRuntime=pc-designer'))
    view.show()
    timer.start()
    QTimer.singleShot(20000, app.quit)
    app.exec()
    view.close()
    server.stop()
    print('Board/GPIO native test:', result)
    return 0 if result['ok'] else 10
