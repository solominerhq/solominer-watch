#!/usr/bin/env python3
"""Regenerate the committed Inter subsets from the included OFL font inputs."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def payload(text):
    bitmap=text.split('glyph_bitmap[] = {')[1].split('};')[0]
    bitmap=re.sub(r'/\*.*?\*/','',bitmap,flags=re.S)
    data=bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]+)',bitmap))
    # Keep all generated data and font metrics; ignore comments and formatting.
    body=text[text.index('static LV_ATTRIBUTE_LARGE_CONST'):]
    body=re.sub(r'/\*.*?\*/','',body,flags=re.S)
    body=re.sub(r'\.static_bitmap\s*=\s*0,', '', body)
    return data,re.sub(r'\s+','',body)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--converter',required=True,type=Path,help='lv_font_conv 1.5.3 lv_font_conv.js')
    ap.add_argument('--node',default='node')
    ap.add_argument('--check',action='store_true',help='Verify without replacing committed C files')
    args=ap.parse_args()
    manifest=json.loads((ROOT/'assets/fonts/MANIFEST.json').read_text())
    import tempfile
    with tempfile.TemporaryDirectory() as folder:
        for rec in manifest['subsets']:
            font=ROOT/'assets/fonts'/rec['source']
            if hashlib.sha256(font.read_bytes()).hexdigest()!=manifest['fonts'][rec['source']]['sha256']:
                raise SystemExit('Font checksum mismatch: '+font.name)
            output=ROOT/rec['output'];temporary=Path(folder)/output.name
            subprocess.run([args.node,str(args.converter),'--bpp','4','--size',str(rec['size']),'--no-compress','--font',str(font),'--range',rec['range'],'--format','lvgl','--lv-font-name',rec['lv_font_name'],'-o',str(temporary)],check=True)
            text=temporary.read_text();a=text.index(' * Opts:');b=text.index('\n',a)
            text=text[:a]+' * Opts: see assets/fonts/MANIFEST.json and scripts/generate_fonts.py\n * Font source: '+font.name+' (SIL OFL 1.1; see assets/fonts/OFL.txt)'+text[b:]
            if args.check:
                if payload(text)!=payload(output.read_text()):raise SystemExit('Generated font differs: '+rec['output'])
            else:output.write_text(text)
            print(('Verified ' if args.check else 'Generated ')+rec['output'])
if __name__=='__main__':main()
