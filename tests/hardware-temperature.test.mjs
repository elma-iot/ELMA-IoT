import test from 'node:test';
import assert from 'node:assert/strict';
import { createHardwareTab } from '../web/modules/hardware-tab.js';

test('temperature card shows sample age and clears expired values', () => {
  const cards = new Map();
  const tab = createHardwareTab({
    state: {},
    elements: new Proxy({}, { get: (_, key) => key }),
    formatBytes: String,
    pinSummary: () => '',
    updateResourceCard: (key, bar, meta, value, percent, explanation) => cards.set(key, { value, percent, explanation }),
  });
  tab.renderDeviceResources({ system: { chipTemperatureAvailable: true, chipTemperatureC: 72.8, chipTemperatureEstimated: true, chipTemperatureAgeMs: 6500 } });
  assert.equal(cards.get('deviceChipTempValue').value, '72.8 C');
  assert.match(cards.get('deviceChipTempValue').explanation, /Estimated internal die temperature/);
  assert.match(cards.get('deviceChipTempValue').explanation, /Last valid sample 6 s ago/);
  const reason = 'Waiting for a valid internal sensor reading; invalid or expired samples are not displayed.';
  tab.renderDeviceResources({ system: { chipTemperatureAvailable: false, chipTemperatureC: null, chipTemperatureReason: reason } });
  assert.deepEqual(cards.get('deviceChipTempValue'), { value: 'Unavailable', percent: 0, explanation: reason });
});
