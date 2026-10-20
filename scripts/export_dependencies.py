"""Prepare a private-review dependency snapshot, exact hashes, and linked-library inventory."""
from pathlib import Path
import json,zipfile,hashlib,re,tarfile
import argparse,os,urllib.request,tempfile,shutil
from firmware_artifacts import version
from check_release_readiness import check_readiness
p=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--output',type=Path,default=p/'dist')
ap.add_argument('--core-dir',type=Path,default=Path(os.environ.get('PLATFORMIO_CORE_DIR',str(Path.home()/'.platformio'))))
ap.add_argument('--upstream-cache',type=Path,default=p/'.pio/dependency-sources')
ap.add_argument('--public',action='store_true')
args=ap.parse_args()
if args.public:check_readiness()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
package=args.core_dir/'packages';framework=package/'framework-arduinoespressif32'
platforms=list((args.core_dir/'platforms').glob('espressif32*'))
platform=next((f for f in platforms if (f/'platform.json').is_file() and json.loads((f/'platform.json').read_text()).get('version')=='6.11.0'),None)
if platform is None:raise SystemExit('Install espressif32 6.11.0 first')
if json.loads((framework/'package.json').read_text()).get('version')!='3.20017.241212+sha.dcc1105b':raise SystemExit('Framework version changed: repeat the dependency audit')
cache=args.upstream_cache.resolve();cache.mkdir(parents=True,exist_ok=True)
upstreams=json.loads((p/'docs/dependency-upstreams.json').read_text())
for rec in upstreams:
    name=rec['file']
    if Path(name).name!=name or not rec['url'].startswith('https://codeload.github.com/'):raise SystemExit('Invalid upstream record')
    dest=cache/name
    if not dest.exists():
        with urllib.request.urlopen(rec['url'],timeout=120) as response: data=response.read()
        if hashlib.sha256(data).hexdigest()!=rec['sha256']:raise SystemExit('Upstream checksum mismatch: '+name)
        dest.write_bytes(data)
    if hashlib.sha256(dest.read_bytes()).hexdigest()!=rec['sha256']:raise SystemExit('Cached checksum mismatch: '+name)
read=lambda f:json.loads(f.read_text())
records={};modules=[]
zip_path=out/('SOLOMINER-'+version(p)+'-dependencies'+('' if args.public else '-PRIVATE-REVIEW')+'.zip')
with zipfile.ZipFile(zip_path,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 def add(f,name):
  b=f.read_bytes();z.writestr(name,b);records[name]=hashlib.sha256(b).hexdigest()
 def tree(base,prefix):
  for f in sorted(base.rglob('*')):
   if not f.is_file() or f.is_symlink() or any(v in {'.git','__pycache__','.DS_Store','.cache'} for v in f.relative_to(base).parts):continue
   add(f,prefix+'/'+f.relative_to(base).as_posix())
 for folder in ['ArduinoJson','WiFiManager','OneButton','NTPClient','GFX Library for Arduino','lvgl','XPowersLib']:
  base=p/'.pio/libdeps/Waveshare-AMOLED-206'/folder
  meta=read(base/'library.json') if (base/'library.json').exists() else {'library.properties':(base/'library.properties').read_text()}
  modules.append({'name':folder,'metadata':meta,'archive_prefix':'libraries/'+folder})
  tree(base,'libraries/'+folder)
 for name in ['cores','libraries','variants','tools/sdk/esp32s3']:
  tree(framework/name,'framework-arduinoespressif32/'+name)
 for f in ['package.json','LICENSE.md','boards.txt','platform.txt']:
  if (framework/f).exists():add(framework/f,'framework-arduinoespressif32/'+f)
 for name in ['tools/platformio-build.py','tools/partitions','tools/ide-debug','tools/espota.py']:
  f=framework/name
  if f.is_dir():tree(f,'framework-arduinoespressif32/'+name)
  elif f.is_file():add(f,'framework-arduinoespressif32/'+name)
 tree(platform,'platform-espressif32-6.11.0')
 for record in upstreams:
  f=cache/record['file']
  if f.suffixes[-2:]!=['.tar','.gz']:continue
  add(f,'upstream/'+f.name)
 # License supplements kept beside exact packages, not substituted for per-file notices.
 tree(p/'licenses','notice-supplements')
 add(p/'docs/dependency-upstreams.json','UPSTREAM_DOWNLOADS.json')
 body='''SOLOMINER dependency and source snapshot\n\nContains the exact installed/patched direct libraries and Arduino source used in\nthe audited build, ESP32-S3 SDK headers/archives/configuration, platform build\nscripts, pinned upstream IDF/source archives and component license texts.\nUPSTREAM_DOWNLOADS.json identifies downloaded source archives.\nSHA256.json covers every payload file. Extract nested archives before use.\n\nThis is not a claim that every SDK binary has public source: Espressif radio/PHY\nlibraries include binary-only objects. The SDK identifies IDF 38eeba213a (4.4.7)\nand the upstream Arduino SDK update identifies its supplemental components.\nThe library-builder applies patches; sources and patch scripts are included.\nAn independent full SDK rebuild has NOT been performed.\n\nUse with the matching SOLOMINER source archive and its docs/DEPENDENCY_AUDIT.md.\nReview the GPL treatment of binary-only SDK components before publishing firmware.\nKeep this archive private while that review and artwork clearance are pending.\nNo commercial song, application firmware binary, credentials or Git history is\nincluded. This archive DOES include unmodified SDK vendor .a libraries.\n'''
 z.writestr('README.txt',body);records['README.txt']=hashlib.sha256(body.encode()).hexdigest()
 z.writestr('SHA256.json',json.dumps(records,indent=2)+'\n')
info={'archive':zip_path.name,'sha256':hashlib.sha256(zip_path.read_bytes()).hexdigest(),'bytes':zip_path.stat().st_size,'payload_files':len(records),'scope':'Private-review dependency/source snapshot; vendor SDK archives included; no application firmware or commercial audio'}
# Archive map: record selected and positively retained libraries separately.
map_text=(p/'.pio/build/Waveshare-AMOLED-206/firmware.map').read_text();top=map_text.split('Discarded input sections')[0]
selected=sorted(set(re.findall(r'^((?:\.pio/|/)[^\n]*?\.a)\(',top,re.M)))
allocated=map_text.split('Linker script and memory map')[1].split('OUTPUT(')[0]
retained=set()
for match in re.finditer(r'^\s+0x([\da-f]+)\s+0x([\da-f]+)\s+([^\n]+?\.a)\(',allocated,re.M):
 if int(match[1],16) and int(match[2],16):retained.add(Path(match[3]).name)
archives=[]
for item in selected:
 f=Path(item) if item.startswith('/') else p/item
 if f.is_relative_to(p):rel=f.relative_to(p).as_posix()
 elif f.is_relative_to(package):rel='platformio-packages/'+f.relative_to(package).as_posix()
 else:raise ValueError('Unexpected archive root')
 archives.append({'path':rel,'sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'bytes':f.stat().st_size,'retained_nonzero_sections':f.name in retained})
manifest={'audit_date':'2026-10-20','environment':'Waveshare-AMOLED-206','platform':'espressif32@6.11.0','arduino_package':read(framework/'package.json'),'sdk_idf_commit':'38eeba213aa695aabfd6d89aa9f5078dbe5a94c3','sdk_idf_version':'4.4.7-dirty','framework_source_note':'Exact installed files archived; historical audit differences documented in DEPENDENCY_AUDIT.md.','dependency_bundle':info,'direct_library_metadata':modules,'selected_archives':archives,'note':'Archive selection is conservative. retained_nonzero_sections means positive nonzero allocated contributions found in the link map; false does not waive source/package notices. ArduinoJson and minimp3 are header-compiled in application objects. SDK and toolchain notices remain separate.'}
(p/'docs/dependency-inventory.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(info,indent=2))
