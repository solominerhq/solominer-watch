"""Export a clean, history-free source review candidate (does not publish)."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import zipfile
from firmware_artifacts import version
from check_release_readiness import check_readiness
from check_public_audio import check_audio_file

ROOT = Path(__file__).resolve().parents[1]
FILES = ['README.md', 'LICENSE', 'LICENSING.md', 'THIRD_PARTY_NOTICES.md', 'CONTRIBUTING.md',
         'CHANGELOG.md', '.gitignore', 'platformio.ini', 'requirements-build.txt',
         'partitions_ota_32MB.csv', 'pre_patch_gfx_lib.py', 'post_build_merge.py',
         'platformio.sleep-test.ini', 'SECURITY.md', 'release-readiness.json']
DIRECTORIES = ['src', 'include', 'examples', 'tests', 'scripts', 'licenses', '.github',
               'assets/setup-portal', 'assets/branding', 'assets/celebration',
               'assets/fonts', 'assets/splash']
DOCS = ['ARCHITECTURE.md', 'HARDWARE_TESTS.md', 'DIAGNOSTICS.md',
        'LIGHT_SLEEP_TEST.md', 'OPEN_SOURCE_RELEASE.md', 'ASSET_PROVENANCE.md',
        'PHONE_SETUP.md', 'USER_GUIDE.md', 'AUDIO_DECODER.md', 'CUSTOM_AUDIO.md',
        'DEPENDENCY_AUDIT.md', 'dependency-inventory.json', 'dependency-upstreams.json']
SKIP = {'__pycache__', '.DS_Store'}

def source_files(root=ROOT):
    root = Path(root).resolve()
    paths = [root / name for name in FILES]
    paths += [root / 'docs' / name for name in DOCS]
    paths.append(root / 'docs' / ('RELEASE_' + version(root) + '.md'))
    for name in DIRECTORIES:
        base = root / name
        if not base.is_dir() or base.is_symlink():
            raise ValueError('Missing directory or unexpected symlink: ' + name)
        for p in base.rglob('*'):
            if p.is_symlink():
                raise ValueError('Unexpected symlink: ' + str(p.relative_to(root)))
            if not p.is_file() or any(part in SKIP for part in p.parts):
                continue
            if p.suffix in {'.pyc', '.log', '.bin', '.elf', '.map'}:
                continue
            paths.append(p)
    for p in paths:
        relative = p.relative_to(root)
        if (not p.is_file() or
                any((root / Path(*relative.parts[:i])).is_symlink()
                    for i in range(1, len(relative.parts) + 1))):
            raise ValueError('Missing file or unexpected symlink: ' + str(relative))
    return sorted(set(paths))

def check_private_material(data, name):
    # High-confidence checks only; this is not a comprehensive secret scanner.
    patterns = [rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----',
                rb'gh[pousr]_[A-Za-z0-9]{30,}',
                rb'github_pat_[A-Za-z0-9_]{40,}', rb'(?:AKIA|ASIA)[A-Z0-9]{16}',
                rb'/' + rb'Users/[^/\s]+/', rb'/' + rb'home/[^/\s]+/',
                rb'[A-Za-z]:\\Users\\[^\\\s]+\\',
                rb'https?://[^/\s:@]+:[^/\s@]+@']
    if any(re.search(pattern, data) for pattern in patterns):
        raise ValueError('Review possible private material in ' + str(name))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--public', action='store_true',
                        help='Require recorded publication decisions before exporting')
    args = parser.parse_args()
    output = args.output.resolve()
    for name in DIRECTORIES + ['docs']:
        base = (ROOT / name).resolve()
        if output == base or base in output.parents:
            parser.error('Output must be outside exported source directories.')
    if args.public:
        check_readiness(source_only=True)
    output.mkdir(parents=True, exist_ok=True)
    suffix = '-source' if args.public else '-source-PRIVATE-REVIEW'
    name = 'SOLOMINER-' + version(ROOT) + suffix
    archive = output / (name + '.zip')
    records = {}
    files = source_files()
    for p in files:
        data = p.read_bytes()
        check_audio_file(p.relative_to(ROOT), data)
        check_private_material(data, p.relative_to(ROOT))
        records[p.relative_to(ROOT).as_posix()] = hashlib.sha256(data).hexdigest()
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in files:
            z.write(p, name + '/' + p.relative_to(ROOT).as_posix())
        z.writestr(name + '/SOURCE_SHA256.json', json.dumps(records, indent=2) + '\n')
    print('Created source candidate:', archive)
    print(len(files), 'files; no Git history, firmware binaries, or build cache.')
    if not args.public:
        print('PRIVATE REVIEW ONLY: complete docs/OPEN_SOURCE_RELEASE.md before publication.')
if __name__ == '__main__':
    main()
