"""Fail before building a monetized Play release with sample IDs or missing signing."""
import os,pathlib,re,sys,urllib.request
from cryptography.hazmat.primitives.serialization import load_der_public_key
import base64
errors=[]
for name,pattern in [('ELMA_ADMOB_APP_ID',r'ca-app-pub-\d{16}~\d{10}'),('ELMA_ADMOB_BANNER_ID',r'ca-app-pub-\d{16}/\d{10}')]:
 value=os.environ.get(name,'')
 if not re.fullmatch(pattern,value) or '3940256099942544' in value:errors.append(name+' must contain your real Console value')
if os.environ.get('ELMA_REMOVE_ADS_PRODUCT','elma_remove_ads') != 'elma_remove_ads':errors.append('The permanent product ID is fixed: elma_remove_ads')
try:load_der_public_key(base64.b64decode(os.environ.get('ELMA_PLAY_PUBLIC_KEY',''),validate=True))
except Exception:errors.append('ELMA_PLAY_PUBLIC_KEY must be the Base64 RSA public licensing key from Play Console')
if not pathlib.Path(os.environ.get('ELMA_ANDROID_KEYSTORE','missing')).is_file():errors.append('ELMA_ANDROID_KEYSTORE must point to your Play upload keystore')
for key in ('ELMA_ANDROID_STORE_PASSWORD','ELMA_ANDROID_KEY_ALIAS','ELMA_ANDROID_KEY_PASSWORD'):
 if not os.environ.get(key):errors.append(key+' is required')
url=os.environ.get('ELMA_PRIVACY_POLICY_URL','')
if not url.startswith('https://'):errors.append('ELMA_PRIVACY_POLICY_URL must be a publicly hosted HTTPS policy')
else:
 try:
  with urllib.request.urlopen(url,timeout=15) as r:
   content=r.read(1024*1024).decode('utf-8','replace')
  if 'ELMA' not in content or 'elik745i@gmail.com' not in content:errors.append('Hosted policy must identify ELMA and the support contact')
 except Exception:errors.append('Public privacy policy URL is unreachable')
if errors:print('\n'.join(errors));sys.exit(1)
print('Release inputs are configured. Production advertising remains disabled in app/build.gradle until explicitly activated after owner configuration. Complete Console declarations and test tracks before publishing.')
