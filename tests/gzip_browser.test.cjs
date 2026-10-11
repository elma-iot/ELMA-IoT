const assert = require('node:assert/strict');
const { spawn } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const https = require('node:https');
const zlib = require('node:zlib');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const python = process.env.GZIP_TEST_PYTHON || 'python';
function request(url, method, accept) {
  return new Promise((resolve, reject) => {
    const transport = url.startsWith('https:') ? https : http;
    const r = transport.request(url, { method, rejectUnauthorized: false, headers: { 'Accept-Encoding': accept } }, response => {
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => resolve({ status: response.statusCode, headers: response.headers, body: Buffer.concat(chunks) }));
      response.on('error', reject);
    });
    r.on('error', reject); r.end();
  });
}
(async () => {
  const fixture = spawn(python, [path.join(__dirname, 'gzip_fixture.py')], { cwd: root });
  fixture.stderr.pipe(process.stderr);
  let browser;
  try {
    const ports = await new Promise((resolve, reject) => {
      fixture.once('error', reject);
      fixture.once('exit', code => reject(new Error(`Fixture exited ${code}`)));
      fixture.stdout.once('data', data => resolve(JSON.parse(data.toString())));
    });
    browser = await chromium.launch({ channel: 'chrome', headless: true });
    // Test fixture certificates are disposable. Production trust is not tested here.
    const context = await browser.newContext({ ignoreHTTPSErrors: true });
    for (const protocol of ['http', 'https']) {
      const base = `${protocol}://localhost:${ports[protocol]}/web/`;
      const page = await context.newPage();
      const errors = []; page.on('pageerror', e => errors.push(e.message));
      await page.goto(base);
      await page.waitForFunction(() => document.querySelector('#result').textContent === 'GZIP rendered correctly');
      assert.equal(await page.locator('#result').evaluate(e => getComputedStyle(e).color), 'rgb(12, 34, 56)');
      assert.deepEqual(errors, []);
      for (const [file, mime] of [['index.html', 'text/html'], ['style.css', 'text/css'], ['app.js', 'application/javascript'], ['data.json', 'application/json']]) {
        const get = await request(base + file, 'GET', 'gzip');
        const encoded = fs.readFileSync(path.join(root, '.web-test/gzip-site/web', file + '.gz'));
        assert.equal(get.status, 200); assert.equal(get.headers['content-encoding'], 'gzip');
        assert.ok(get.headers['content-type'].startsWith(mime));
        assert.equal(get.headers.vary, 'Accept-Encoding');
        assert.equal(Number(get.headers['content-length']), encoded.length);
        assert.deepEqual(get.body, encoded); assert.ok(zlib.gunzipSync(get.body).length);
        const head = await request(base + file, 'HEAD', 'gzip');
        assert.equal(head.status, 200); assert.equal(head.body.length, 0);
        assert.equal(head.headers['content-length'], get.headers['content-length']);
        assert.equal((await request(base + file, 'GET', 'gzip;q=0')).status, 406);
      }
      const plain = await request(base + 'plain.js', 'GET', 'identity');
      assert.equal(plain.status, 200); assert.equal(plain.headers['content-encoding'], undefined);
      assert.equal(plain.body.toString(), 'window.plain = true;');
      await page.close();
      console.log(`PASS Chrome ${protocol.toUpperCase()}: gzip-only HTML/CSS/JS/JSON, MIME, exact compressed bytes, HEAD, 406 and plain fallback`);
    }
  } finally { if (browser) await browser.close(); fixture.kill(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
