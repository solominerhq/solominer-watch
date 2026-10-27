#!/usr/bin/env python3
"""Run portable behavior tests after PlatformIO has installed ArduinoJson."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='solominer-battery-tests-') as temp:
    binary=Path(temp)/'battery-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror',
        '-I'+str(root/'src'),str(root/'tests/battery_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
json_include=root/'.pio/libdeps/Waveshare-AMOLED-206/ArduinoJson/src'
if not json_include.exists():
    raise SystemExit('Install dependencies first: pio pkg install -e Waveshare-AMOLED-206')
with tempfile.TemporaryDirectory(prefix='solominer-tests-') as temp:
    binary=Path(temp)/'policy-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror',
        '-I'+str(root/'tests/support'),'-I'+str(root/'src'),'-I'+str(json_include),
        str(root/'tests/policy_tests.cpp'),str(root/'src/settings/settings.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='solominer-network-tests-') as temp:
    binary=Path(temp)/'network-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror',
        '-I'+str(root/'tests/network_support'),'-I'+str(root/'src'),
        str(root/'tests/network_runtime_tests.cpp'),str(root/'src/network/wifi_connection.cpp'),
        '-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix="solominer-celebration-tests-") as temp:
    binary=Path(temp)/"block-found-tests"
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++11","-Wall","-Wextra","-Werror",
        "-I"+str(root/"src"),str(root/"tests/block_found_sequence_tests.cpp"),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='solominer-power-tests-') as temp:
    binary=Path(temp)/'power-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror',
        '-I'+str(root/'tests/network_support'),'-I'+str(root/'src'),
        str(root/'tests/power_runtime_tests.cpp'),str(root/'src/device/device_power.cpp'),
        str(root/'src/device/mining_control.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='solominer-sleep-tests-') as temp:
    binary=Path(temp)/'sleep-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-pthread',
        '-I'+str(root/'src'),str(root/'tests/sleep_policy_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True,timeout=15)

with tempfile.TemporaryDirectory(prefix='solominer-wifi-wake-') as temp:
    binary=Path(temp)/'wifi-wake-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(root/'tests/wifi_display_wake_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='solominer-fresh-rate-') as temp:
    binary=Path(temp)/'rate-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(root/'tests/hashrate_sample_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='solominer-touch-') as temp:
    binary=Path(temp)/'touch-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(root/'tests/touch_input_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='solominer-block-record-') as temp:
    binary=Path(temp)/'block-record-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(root/'tests/block_record_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

with tempfile.TemporaryDirectory(prefix='solominer-reliability-') as temp:
    binary=Path(temp)/'reliability-tests'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(root/'src'),str(root/'tests/reliability_tests.cpp'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
