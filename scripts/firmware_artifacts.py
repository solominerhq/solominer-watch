"""Deterministic artifacts for the supported ESP32-S3 partition layout."""
from pathlib import Path
import hashlib, json, re

def version(project):
    match=re.search(r'#define\s+CURRENT_VERSION\s+"([A-Za-z0-9._-]+)"', (Path(project)/'src/version.h').read_text())
    if not match: raise ValueError('Missing or unsafe CURRENT_VERSION')
    return match.group(1)

def merge_segments(segments):
    result=bytearray()
    for offset,data,limit in sorted(segments,key=lambda item:item[0]):
        if not data or len(data)>limit or offset<len(result):
            raise ValueError('Missing, overlapping or oversized firmware segment')
        result.extend(b'\xff'*(offset-len(result)))
        result.extend(data)
    result.extend(b'\xff'*((-len(result))%4096))
    return bytes(result)

def create(project, build, framework, environment):
    project,build,framework=map(Path,(project,build,framework))
    # Layout matches partitions_ota_32MB.csv; fail rather than guess a chip from size.
    layout=project/'partitions_ota_32MB.csv'
    app_line=next(line for line in layout.read_text().splitlines() if line.startswith('app0,'))
    fields=[x.strip() for x in app_line.split(',')]
    offset,capacity=int(fields[3],0),int(fields[4],0)
    if offset!=0x10000: raise ValueError('Unsupported application offset')
    app=(build/'firmware.bin').read_bytes()
    factory=merge_segments([
        (0,(build/'bootloader.bin').read_bytes(),0x8000),
        (0x8000,(build/'partitions.bin').read_bytes(),0x1000),
        (0xe000,(framework/'tools/partitions/boot_app0.bin').read_bytes(),0x2000),
        (offset,app,capacity)])
    # Fail the release build if a compiler/library reintroduces this builder's
    # private path. Vendor-precompiled libraries can have unrelated build paths.
    for private_path in (str(Path.home()), str(project.resolve())):
        if private_path.encode() in factory:
            raise ValueError('Firmware contains a private build path; check prefix mappings')
    output=project/'firmware'/version(project)
    output.mkdir(parents=True,exist_ok=True)
    portal_name = 'SOLOMINER-' + version(project) + '.bin'
    files={environment+'_factory.bin':factory,environment+'_firmware.bin':app,
           portal_name:app}
    manifest={'version':version(project),'environment':environment,'files':{}}
    audio_manifest = build / 'celebration_audio.json'
    if audio_manifest.exists():
        manifest['celebration_audio'] = json.loads(audio_manifest.read_text())
    for name,data in files.items():
        (output/name).write_bytes(data)
        manifest['files'][name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    (output/(environment+'_manifest.json')).write_text(json.dumps(manifest,indent=2)+'\n')
    print('Verified firmware artifacts:',output)
    return output
