# Precompressed custom web UI

In the existing External storage tab, enable **Use Custom Web Interface**
and save configuration. This option defaults to off. Place your entry point at
`/web/index.html` or `/web/index.html.gz` on the SD card. Opening `/` redirects
to `/web/` when that entry point is available and acceptable; otherwise the
built-in UI remains available. `/system/ui` always opens the built-in recovery
interface. API, WebSocket and system routes cannot be replaced by SD files.

Prepare gzip files on a PC before uploading. For example:

```python
from pathlib import Path
import gzip

for name in ("index.html", "style.css", "app.js", "data.json", "logo.svg"):
    source = Path(name)
    Path(str(source) + ".gz").write_bytes(gzip.compress(source.read_bytes(), mtime=0))
```

Keep uncompressed copies when clients without gzip support must be supported.
The device never compresses at boot or card insertion, inflates files into RAM,
or transforms templates inside compressed files. Use relative asset URLs and
same-origin API/WebSocket URLs in a custom UI to preserve its current transport.
Only enable a custom UI from a source you trust: its JavaScript runs with the
device UI's origin and access to its APIs.

## Response behavior

* An explicit accepted `gzip` encoding (or accepted wildcard) selects the `.gz`
  sibling first. `gzip;q=0` excludes gzip even when a wildcard is present.
* Otherwise an acceptable uncompressed sibling is served. Gzip-only files
  return `406` when gzip is excluded or not advertised; missing files return
  `404`. Invalid gzip headers return `500`.
* MIME types are derived from the original filename. Responses include
  `Vary: Accept-Encoding`, encoded `Content-Length`, and `Content-Encoding:
  gzip` only for gzip representations. `HEAD` includes the same metadata
  without a body or storage-reader task.
* HTML keeps the existing no-store shell policy. Other files use revalidation
  with representation-specific weak ETags derived from path, modification time,
  encoded size and encoding. Files with no modification timestamp use no-store
  and omit ETags. FAT timestamp resolution means same-size replacements within
  the same timestamp interval can retain an ETag; use versioned filenames when
  deploying updates. Conditional requests accept ETag lists and weak tags.
* Stored representations are limited to 16 MiB. Range responses are unsupported.
  Storage readers retain the existing two-stream limit, 4 KiB ring and internal
  RAM reserve; TCP staging is bounded to one 1,460-byte packet, with at most two
  packets in flight. Slow readers yield instead of blocking the network task.
  Requests exceeding the reader limit or available memory return `503`; retry
  after active downloads finish.
* Removal, premature EOF or read failure closes the connection when fewer bytes
  than advertised are read. A truncated response is never marked successful.
  An unavailable SD card falls back to the built-in UI at `/`.
* The normal embedded UI uses its existing build-time gzip files and content-hash
  ETags, now with encoding negotiation and body-free `HEAD`. The minimal legacy
  OTA page remains a plain static page.

The response implementation is transport-independent and does not perform
runtime compression, so TLS must send the same compressed bytes and headers.
This change does not implement an ESP32 HTTPS listener, mDNS or certificate
provisioning. These still require the separately proposed networking work.

## Validation

`tests/static_assets_host.cpp` compiles the actual negotiation and bounded
response code with a small desktop AsyncWebServer stub. It tests quality values,
fallbacks, traversal rejection, ETag comparisons, partial TCP writes, delayed
readers, `HEAD` and interrupted reads. On Windows with MSVC Build Tools:

```powershell
cmd /d /c '"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cl /nologo /EHsc /std:c++17 /I tests\static_asset_stubs tests\static_assets_host.cpp /Fe:.web-test\static-assets-host.exe /Fo:.web-test\static-assets-host.obj'
.\.web-test\static-assets-host.exe
```

`tests/gzip_browser.test.cjs` requires local Playwright, Chrome and a Python
interpreter with `cryptography`. Set `GZIP_TEST_PYTHON` to that interpreter and
run `node tests/gzip_browser.test.cjs`. It renders gzip-only HTML, CSS, JavaScript
and JSON in Chrome over loopback HTTP and HTTPS; checks MIME types, exact
compressed bytes (no double compression), lengths, `HEAD`, `406` and identity
fallback. The HTTPS fixture uses a disposable certificate and explicitly ignores
its trust error; it does not validate production certificate trust or ESP32 TLS.

On a physical board, additionally verify SD removal mid-transfer, simultaneous
asset loads, OTA recovery and HTTPS once its listener is implemented. Browser
DevTools should show gzip response headers and no MIME or mixed-content errors.
