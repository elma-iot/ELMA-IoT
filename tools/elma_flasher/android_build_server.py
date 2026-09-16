"""Explicitly started, authenticated TLS compilation service for the Android app."""
import datetime
import hashlib
import hmac
import io
import json
import pathlib
import secrets
import shutil
import socket
import ssl
import threading
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def make_bundle(project, job, settings, chip):
    build = pathlib.Path(project) / '.pio/build' / job.profile
    parts = [(0 if chip != 'esp32' else 0x1000, 'bootloader.bin', (build/'bootloader.bin').read_bytes()),
             (0x8000, 'partitions.bin', (build/'partitions.bin').read_bytes()),
             (0xe000, 'ota-reset.bin', b'\xff' * 8192),
             (0x10000, 'firmware.bin', pathlib.Path(job.firmware_file).read_bytes())]
    manifest = {'format': 'elma-usb-bundle-v1', 'version': '0.1.43', 'chip': chip,
                'board': settings['ui']['gpioBoardSelection'], 'parts': []}
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        for address, name, data in parts:
            archive.writestr(name, data)
            manifest['parts'].append({'address': address, 'file': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        archive.writestr('settings.json', json.dumps(settings, separators=(',', ':'), ensure_ascii=False))
        archive.writestr('manifest.json', json.dumps(manifest))
    return output.getvalue()


def certificate(home):
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import NameOID
    home.mkdir(parents=True, exist_ok=True)
    cert_path, key_path = home/'certificate.pem', home/'private-key.pem'
    if not cert_path.exists() or not key_path.exists():
        key = ec.generate_private_key(ec.SECP256R1())
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'ELMA Android Build Service')])
        now = datetime.datetime.now(datetime.timezone.utc)
        cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
                .serial_number(x509.random_serial_number()).not_valid_before(now-datetime.timedelta(minutes=5))
                .not_valid_after(now+datetime.timedelta(days=3650)).sign(key, hashes.SHA256()))
        key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    cert = x509.load_pem_x509_certificate(cert_path.read_bytes())
    return cert_path, key_path, cert.fingerprint(hashes.SHA256()).hex()


class AndroidBuildService:
    def __init__(self, backend, home, board_profiles, sanitize, host='0.0.0.0', port=0):
        self.backend, self.home = backend, pathlib.Path(home)
        self.boards, self.sanitize = board_profiles, sanitize
        self.lock = threading.Lock()
        self.jobs = {}
        cert, key, self.pin = certificate(self.home)
        token_path = self.home/'pairing-token.txt'
        if not token_path.exists(): token_path.write_text(secrets.token_urlsafe(32), encoding='ascii')
        self.token = token_path.read_text(encoding='ascii').strip()
        owner = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_): pass
            def send(self, body, status=200, content_type='application/json'):
                data = body if isinstance(body, bytes) else json.dumps(body).encode()
                self.send_response(status); self.send_header('Content-Type', content_type)
                self.send_header('Content-Length', str(len(data))); self.send_header('Cache-Control', 'no-store')
                self.end_headers(); self.wfile.write(data)
            def handle_request(self):
                if not hmac.compare_digest(self.headers.get('Authorization', ''), 'Bearer '+owner.token):
                    self.send({'error':'Pair this phone with ELMA Flasher first.'},401); return
                try:
                    path = self.path.split('?',1)[0]
                    if self.command == 'GET' and path == '/v1/status':
                        self.send({'version':'0.1.43','compiler':'ELMA PC','ready':True}); return
                    if self.command == 'POST' and path == '/v1/build':
                        length = int(self.headers.get('Content-Length','0'))
                        if length <= 0 or length > 2*1024*1024: raise ValueError('Invalid configuration size')
                        self.send(owner.start_build(json.loads(self.rfile.read(length))),202); return
                    pieces = path.strip('/').split('/')
                    if len(pieces) in (3,4) and pieces[:2] == ['v1','jobs']:
                        with owner.lock: item = owner.jobs.get(pieces[2])
                        if item is None: self.send({'error':'Unknown build'},404); return
                        job = item['job']
                        if self.command == 'GET' and len(pieces)==3:
                            value=job.public(); value.pop('firmwareFile',None)
                            self.send(value); return
                        if self.command == 'POST' and len(pieces)==4 and pieces[3]=='cancel':
                            job.cancelled=True; owner.backend.terminate_job_process(job); self.send({'ok':True}); return
                        if self.command == 'GET' and len(pieces)==4 and pieces[3]=='bundle':
                            if job.state != 'complete': self.send({'error':'Compilation is not complete'},409); return
                            with owner.lock:
                                if 'bundle' not in item:
                                    item['bundle']=make_bundle(owner.backend.project_root(),job,item['settings'],item['chip'])
                                bundle=item['bundle']
                            self.send(bundle,content_type='application/zip'); return
                    self.send({'error':'Unknown endpoint'},404)
                except (ValueError, KeyError, TypeError) as error: self.send({'error':str(error)},400)
                except Exception: self.send({'error':'Build service failed. Check the PC build log.'},500)
            do_GET = handle_request
            do_POST = handle_request

        self.httpd = ThreadingHTTPServer((host,port),Handler)
        self.httpd.daemon_threads=True
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); context.minimum_version=ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(cert,key); self.httpd.socket=context.wrap_socket(self.httpd.socket,server_side=True)
        self.thread=threading.Thread(target=self.httpd.serve_forever,daemon=True)

    def start_build(self, body):
        settings=body.get('settings')
        if not isinstance(settings,dict): raise ValueError('Settings must be a JSON object')
        board=settings.get('ui',{}).get('gpioBoardSelection')
        if board not in self.boards: raise ValueError('Select a supported board in Designer')
        chip=self.boards[board][1]
        if chip not in ('esp32','esp32s3','esp32c3'): raise ValueError('Compiler supports ESP32, ESP32-S3 and ESP32-C3')
        # Paths, commands and flash actions are never accepted from the network.
        safe=self.sanitize(settings)
        safe.setdefault('ui',{})['gpioBoardSelection']=board
        self.home.mkdir(parents=True,exist_ok=True)
        output=self.home/('firmware-'+secrets.token_hex(8)+'.bin')
        with self.lock:
            # Materialize older completed bundles before a new build reuses the
            # compiler output directory, so their parts can never be mixed.
            for item in self.jobs.values():
                if item['job'].state == 'complete' and 'bundle' not in item:
                    item['bundle']=make_bundle(self.backend.project_root(),item['job'],item['settings'],item['chip'])
            for ident in list(self.jobs)[:-2]:
                old=self.jobs[ident]
                if old['job'].state not in ('running','queued'):
                    path=pathlib.Path(old['job'].firmware_file)
                    if path.parent.resolve()==self.home.resolve(): path.unlink(missing_ok=True)
                    del self.jobs[ident]
            job=self.backend.create_job({'compileOnly':True,'transport':'usb','chip':chip,'firmwareMode':'full',
                'capabilities':{'maximum':True,'webUi':True,'hacs':True,'audio':True},
                'settings':safe,'outputPath':str(output)})
            self.jobs[job.id]={'job':job,'settings':safe,'chip':chip}
        return {'jobId':job.id}

    def start(self): self.thread.start()
    def stop(self):
        for item in self.jobs.values():
            if item['job'].state in ('running','queued'):
                item['job'].cancelled=True; self.backend.terminate_job_process(item['job'])
        self.httpd.shutdown(); self.httpd.server_close()
    def pairing(self, host):
        return json.dumps({'url':f'https://{host}:{self.httpd.server_port}','token':self.token,'certificateSha256':self.pin},separators=(',',':'))


def run_window(backend_class, boards, sanitize):
    import tkinter as tk
    from tkinter import ttk
    home=backend_class.portable_home()/'android-build-service'
    # A separate project avoids racing the normal desktop Designer compiler.
    class Backend(backend_class):
        def project_root(self):
            if not hasattr(self,'android_project'):
                source=super().project_root(); destination=home/'project'
                destination.mkdir(parents=True,exist_ok=True)
                for name in ('src','include','scripts','partitions','web'):
                    shutil.copytree(source/name,destination/name,dirs_exist_ok=True)
                for name in ('platformio.ini','sdkconfig.defaults','package.json','package-lock.json'):
                    if (source/name).exists(): shutil.copyfile(source/name,destination/name)
                self.android_project=destination
            return self.android_project
    backend=Backend(None)
    service=AndroidBuildService(backend,home,boards,sanitize,port=18743)
    service.start()
    addresses=sorted({v[4][0] for v in socket.getaddrinfo(socket.gethostname(),None,socket.AF_INET) if not v[4][0].startswith('127.')}) or ['127.0.0.1']
    root=tk.Tk(); root.title('ELMA Android PC Compiler'); root.geometry('640x290')
    ttk.Label(root,text='Pair your Android app with this PC',font=('Segoe UI',16)).pack(padx=20,pady=16)
    ttk.Label(root,text='Both devices must be on the same local network. Keep this window open.').pack()
    address=tk.StringVar(value=addresses[0]); ttk.Combobox(root,textvariable=address,values=addresses,state='readonly').pack(pady=12)
    def copy(): root.clipboard_clear(); root.clipboard_append(service.pairing(address.get())); status.set('Pairing details copied. Paste them into Build → Pair PC in the Android app.')
    ttk.Button(root,text='Copy pairing details',command=copy).pack()
    status=tk.StringVar(value='TLS encryption and a private pairing token protect build requests.')
    ttk.Label(root,textvariable=status,wraplength=590).pack(padx=20,pady=16)
    try: root.mainloop()
    finally: service.stop()
    return 0
