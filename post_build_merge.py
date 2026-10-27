"""PlatformIO post action: emit verified factory and application images."""
import sys
from pathlib import Path
Import("env")
sys.path.insert(0,str(Path(env.subst("$PROJECT_DIR"))/'scripts'))
from firmware_artifacts import create

def create_merged_firmware(source,target,env):
    if env.BoardConfig().get('build.mcu')!='esp32s3':
        raise RuntimeError('This project supports the Waveshare ESP32-S3 watch only')
    create(env.subst('$PROJECT_DIR'),env.subst('$BUILD_DIR'),
           env.PioPlatform().get_package_dir('framework-arduinoespressif32'),env.subst('$PIOENV'))

env.AddPostAction('$BUILD_DIR/firmware.bin',create_merged_firmware)
