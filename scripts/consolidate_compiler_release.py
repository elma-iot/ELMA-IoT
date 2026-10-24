"""Consolidate checksum-matched SDK assets from drafts inside the same repo.

Runs in GitHub Actions with only that repository's short-lived GITHUB_TOKEN.
No user credentials, application settings or compiled device images are used.
"""
import concurrent.futures
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

REPO=os.environ['GITHUB_REPOSITORY']
TOKEN=os.environ['GITHUB_TOKEN']
BASE='https://api.github.com/repos/'+REPO
HEADERS={'Authorization':'Bearer '+TOKEN,'Accept':'application/vnd.github+json','User-Agent':'ELMA-compiler-release','X-GitHub-Api-Version':'2022-11-28'}


def api(path,method='GET',body=None):
    data=json.dumps(body).encode() if body is not None else None
    request=urllib.request.Request(BASE+path,data=data,headers={**HEADERS,'Content-Type':'application/json'},method=method)
    with urllib.request.urlopen(request,timeout=60) as response:return json.load(response)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args):return None


def download(asset,target):
    # Do not forward the repository token to the signed release CDN URL.
    request=urllib.request.Request(asset['url'],headers={**HEADERS,'Accept':'application/octet-stream'})
    try:response=urllib.request.build_opener(NoRedirect).open(request,timeout=60)
    except urllib.error.HTTPError as error:
        if error.code not in (301,302,303,307,308):raise
        url=error.headers['Location'];parsed=urllib.parse.urlparse(url)
        if parsed.scheme!='https' or parsed.hostname not in ('release-assets.githubusercontent.com','objects.githubusercontent.com'):raise ValueError('Unexpected asset redirect host')
        response=urllib.request.urlopen(url,timeout=90)
    digest=hashlib.sha256();received=0
    with response,target.open('wb') as stream:
        for block in iter(lambda:response.read(4*1024*1024),b''):
            stream.write(block);digest.update(block);received+=len(block)
    if received!=asset['size']:raise ValueError('Incomplete draft asset: '+asset['name'])
    return digest.hexdigest()


def upload(release,path):
    endpoint=urllib.parse.urlparse(release['upload_url'].split('{')[0])
    if endpoint.scheme!='https' or endpoint.hostname!='uploads.github.com':raise ValueError('Unexpected upload host')
    connection=http.client.HTTPSConnection(endpoint.hostname,timeout=300)
    try:
        connection.putrequest('POST',endpoint.path+'?'+urllib.parse.urlencode({'name':path.name}))
        for key,value in {**HEADERS,'Content-Type':'application/octet-stream','Content-Length':str(path.stat().st_size)}.items():connection.putheader(key,value)
        connection.endheaders();last=time.monotonic();sent=0
        with path.open('rb') as stream:
            for block in iter(lambda:stream.read(4*1024*1024),b''):
                connection.send(block);sent+=len(block)
                if time.monotonic()-last>=15:print(path.name,round(sent*100/path.stat().st_size,1),'% uploaded',flush=True);last=time.monotonic()
        response=connection.getresponse();data=response.read()
        if response.status!=201:raise RuntimeError('GitHub upload failed: '+str(response.status)+' '+data[:250].decode(errors='replace'))
        return json.loads(data)
    finally:connection.close()


def main():
    identity=os.environ['SDK_RELEASE_ID']
    if not identity.isdigit():raise ValueError('Invalid release identity')
    release=api('/releases/'+identity)
    if not release['draft']:raise ValueError('Consolidation requires an unpublished draft')
    manifest_asset=next(asset for asset in release['assets'] if asset['name']=='compiler-packages.json')
    with tempfile.TemporaryDirectory() as directory:
        path=Path(directory)/'compiler-packages.json';digest=download(manifest_asset,path)
        if digest!=os.environ['SDK_MANIFEST_SHA256']:raise ValueError('Unexpected manifest checksum')
        manifest=json.loads(path.read_text(encoding='utf8'))
    if manifest['bundle']!=release['tag_name']:raise ValueError('Manifest/release mismatch')
    expected={}
    for pack in manifest['packages'].values():
        for source in pack['downloads']:
            prefix='https://github.com/'+REPO+'/releases/download/'+release['tag_name']+'/'
            if source['url'].startswith(prefix):
                name=source['url'][len(prefix):]
                if not re.fullmatch(r'[a-z0-9.-]+',name):raise ValueError('Invalid asset name')
                expected[name]=(source['sha256'],source['bytes'])
    drafts=[item for item in api('/releases?per_page=100') if item['tag_name']==release['tag_name']]
    source_assets=[asset for item in drafts for asset in item['assets']]
    installed={asset['name']:asset for asset in release['assets']}
    def transfer(pair):
        name,(checksum,size)=pair;existing=installed.get(name)
        if existing and existing.get('digest')=='sha256:'+checksum and existing['size']==size:
            print('Verified existing',name,flush=True);return
        if existing:raise ValueError('Conflicting canonical asset: '+name)
        source=next(asset for asset in source_assets if asset['name']==name and asset['size']==size and asset.get('digest')=='sha256:'+checksum)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/name
            print('Consolidating',name,flush=True)
            if download(source,path)!=checksum:raise ValueError('Draft asset checksum mismatch: '+name)
            uploaded=upload(release,path)
            if uploaded.get('digest')!='sha256:'+checksum:raise ValueError('Uploaded checksum mismatch: '+name)
        print('Verified consolidated',name,flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:list(pool.map(transfer,expected.items()))
    final=api('/releases/'+identity);assets={asset['name']:asset for asset in final['assets']}
    for name,(checksum,size) in expected.items():
        if assets[name].get('digest')!='sha256:'+checksum or assets[name]['size']!=size:raise ValueError('Final asset mismatch: '+name)
    for name in ('compiler-packages.json','SHA256SUMS.txt','README.txt'):
        if name not in assets:raise ValueError('Missing release metadata: '+name)
    api('/releases/'+identity,'PATCH',{'draft':False})
    # Fresh public readback, without the workflow token.
    with urllib.request.urlopen(BASE+'/releases/tags/'+release['tag_name'],timeout=60) as response:public=json.load(response)
    public_assets={asset['name']:asset for asset in public['assets']}
    if public['draft']:raise ValueError('Release is not public')
    for name,(checksum,size) in expected.items():
        if public_assets[name].get('digest')!='sha256:'+checksum or public_assets[name]['size']!=size:raise ValueError('Public release verification failed: '+name)
    print('Public SDK release verified:',public['html_url'],flush=True)


if __name__=='__main__':main()
