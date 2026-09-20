import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

const root=path.resolve(import.meta.dirname,'..');
const generated=fs.readFileSync(path.join(root,'src/generated_web_assets.cpp'),'utf8');

test('portable Russian firmware locale excludes application-only translations',()=>{
  const match=generated.match(/const uint8_t locales_ru_firmware_i18n_js\[\] PROGMEM = \{([\s\S]*?)\};/);
  assert.ok(match,'Russian firmware locale is embedded');
  const compressedBytes=(match[1].match(/0x[0-9a-f]{2}/gi)||[]).length;
  assert.ok(compressedBytes<40000,`Russian firmware locale grew to ${compressedBytes} bytes`);
});
