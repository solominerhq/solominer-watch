"""PlatformIO pre-build: embed only an explicitly supplied local track."""
import sys
from pathlib import Path
Import('env')
root = Path(env.subst('$PROJECT_DIR'))
sys.path.insert(0, str(root / 'scripts'))
from celebration_audio import generate, public_build
build = Path(env.subst('$BUILD_DIR'))
info = generate(root, build, public=public_build())
env.Append(CPPPATH=[str(build / 'generated')])
print('Celebration audio: %s (%s bytes)' % (info['mode'], info['bytes']))
