import assert from 'node:assert/strict';
import {filterBoardNetworks} from '../../Android/web/android-network.js';
const networks=[{ssid:'dual',frequency:2412,rssi:-70},{ssid:'dual',frequency:5180,rssi:-30},{ssid:'5-only',frequency:5805},{ssid:'6-only',frequency:5975},{ssid:'channel14',frequency:2484},{ssid:'unknown'}];
for(const board of ['esp32-c3','esp32-s3-super-mini','wemos-lolin32-mini','unknown'])assert.deepEqual(filterBoardNetworks(networks,board).map(n=>n.ssid),['dual','channel14']);
console.log('PASS 2.4 GHz filtering before SSID aggregation; excludes 5/6 GHz and unknown frequency');
