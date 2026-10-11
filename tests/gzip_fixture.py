"""Loopback browser fixture using the production policy/response host executable.

Requires cryptography for a disposable TLS certificate. This models transport,
not ESP32 filesystem, authentication, or certificate provisioning.
"""
import datetime
import gzip
import json
import pathlib
import socketserver
import ssl
import subprocess
import threading
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID

root = pathlib.Path(__file__).resolve().parents[1]
site = root / '.web-test' / 'gzip-site'
web = site / 'web'
web.mkdir(parents=True, exist_ok=True)
assets = {
    'index.html': b'<!doctype html><link rel="stylesheet" href="style.css"><h1 id="result">Loading</h1><script src="app.js"></script>',
    'style.css': b'#result { color: rgb(12, 34, 56); }',
    'app.js': b'fetch("data.json").then(r=>r.json()).then(d=>document.querySelector("#result").textContent=d.message);',
    'data.json': b'{"message":"GZIP rendered correctly"}',
}
for name, data in assets.items():
    (web / (name + '.gz')).write_bytes(gzip.compress(data, mtime=0))
(web / 'plain.js').write_bytes(b'window.plain = true;')
(web / 'plain.js.gz').write_bytes(gzip.compress(b'window.plain = true;', mtime=0))

key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'localhost')])
now = datetime.datetime.now(datetime.timezone.utc)
cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
        .public_key(key.public_key()).serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=1))
        .not_valid_after(now + datetime.timedelta(days=1))
        .add_extension(x509.SubjectAlternativeName([x509.DNSName('localhost')]), False)
        .sign(key, hashes.SHA256()))
cert_path, key_path = site / 'test-cert.pem', site / 'test-key.pem'
cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        data = b''
        while b'\r\n\r\n' not in data:
            chunk = self.request.recv(4096)
            if not chunk:
                return
            data += chunk
            if len(data) > 65536:
                return
        lines = data.decode('latin1').split('\r\n')
        method, path, _ = lines[0].split(' ')
        if not path.startswith('/web/'):
            self.request.sendall(b'HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
            return
        accept = next((v.split(':', 1)[1].strip() for v in lines[1:] if v.lower().startswith('accept-encoding:')), '')
        response = subprocess.check_output([str(root / '.web-test/static-assets-host.exe'), '--wire', str(site), accept, path.split('?')[0], method])
        self.request.sendall(response)

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

http = Server(('127.0.0.1', 0), Handler)
https = Server(('127.0.0.1', 0), Handler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(cert_path, key_path)
https.socket = context.wrap_socket(https.socket, server_side=True)
for server in (http, https):
    threading.Thread(target=server.serve_forever, daemon=True).start()
print(json.dumps({'http': http.server_address[1], 'https': https.server_address[1]}), flush=True)
try:
    threading.Event().wait()
except KeyboardInterrupt:
    pass
finally:
    http.shutdown()
    https.shutdown()
