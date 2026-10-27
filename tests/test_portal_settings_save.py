"""Run the production portal save callback before any Wi-Fi/portal completion.

Use the real settings codec and timezone application; only HTTP parameters and
flash storage are host adapters. This catches a Save that merely sets a flag.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include "settings/config_codec.h"
#define SERIAL_PRINTLN(...) ((void)0)
#define SERIAL_PRINTF(...) ((void)0)
TSettings Settings;
bool shouldSaveConfig = false;
struct Param {
    std::string value;
    const char* getValue() const { return value.c_str(); }
};
struct Manager {
    std::function<void()> saved;
    void setSaveParamsCallback(std::function<void()> callback) { saved = callback; }
} wm;
struct Storage {
    std::string persisted;
    unsigned writes = 0;
    bool fail = false;
    bool saveConfig(TSettings* settings) {
        ++writes;
        if (fail) return false;
        settings::normalize(*settings);
        StaticJsonDocument<1024> doc;
        settings::writeConfig(doc.to<JsonObject>(), *settings);
        assert(!doc.overflowed());
        persisted.clear(); serializeJson(doc, persisted);
        return true;
    }
    void reboot() {
        Settings = TSettings{};
        StaticJsonDocument<1024> doc;
        assert(!deserializeJson(doc, persisted));
        settings::readConfig(doc.as<JsonObjectConst>(), Settings, settings::ConfigFormat::Internal);
    }
} nvMem;
void checkLocal(time_t epoch, int month, int day, int hour, int dst) {
    struct tm local;
    assert(localtime_r(&epoch, &local));
    assert(local.tm_year + 1900 == 2026 && local.tm_mon + 1 == month);
    assert(local.tm_mday == day && local.tm_hour == hour && local.tm_min == 31);
    assert((local.tm_isdst > 0) == (dst != 0));
}
'''

PARAMETERS = r'''
int main() {
    Param pool_text_box{"public-pool.io"}, port_text_box_num{"21496"};
    Param password_text_box{"x"}, addr_text_box{"  example-wallet  "};
    Param worker_text_box{" Wrist "}, tz_select{"PST8PDT,M3.2.0,M11.1.0"};
    Param clock_select{"24"};
    strlcpy(Settings.PosixTZ, "UTC0", sizeof(Settings.PosixTZ));
    applyTimezone();
'''

CHECKS = r'''
    // No SSID/password change and no portal exit or connection callback.
    // The browser's successful Save must already be durable at this point.
    wm.saved();
    assert(shouldSaveConfig && nvMem.writes == 1 && !nvMem.persisted.empty());
    assert(!strcmp(Settings.PosixTZ, "PST8PDT,M3.2.0,M11.1.0"));
    assert(Settings.clockFormat == 24);
    assert(!strcmp(getenv("TZ"), Settings.PosixTZ));
    checkLocal(1792027860, 10, 14, 18, 1); // Thu 01:31 UTC -> Wed 18:31 PDT.
    // Restart immediately, without ever connecting to Wi-Fi.
    nvMem.reboot(); applyTimezone();
    assert(!strcmp(Settings.PosixTZ, "PST8PDT,M3.2.0,M11.1.0"));
    assert(Settings.clockFormat == 24);
    assert(!strcmp(Settings.BtcWallet, "example-wallet"));
    assert(!strcmp(Settings.WorkerName, "Wrist"));
    checkLocal(1792027860, 10, 14, 18, 1);
    checkLocal(1797301860, 12, 14, 18, 0); // Pacific standard time in winter.

    // A retry, Wi-Fi failure, or empty optional selection must not clear TZ.
    tz_select.value = ""; clock_select.value = "12";
    wm.saved(); nvMem.reboot(); applyTimezone();
    assert(!strcmp(Settings.PosixTZ, "PST8PDT,M3.2.0,M11.1.0"));
    assert(Settings.clockFormat == 12);
    tz_select.value = std::string(48, 'x'); clock_select.value = "99";
    wm.saved(); nvMem.reboot(); applyTimezone();
    assert(!strcmp(Settings.PosixTZ, "PST8PDT,M3.2.0,M11.1.0"));
    assert(Settings.clockFormat == 12);

    // A failed flash write preserves both the old saved and active settings.
    const auto previous = nvMem.persisted;
    nvMem.fail = true; tz_select.value = "EST5EDT,M3.2.0,M11.1.0";
    clock_select.value = "24"; wm.saved();
    assert(nvMem.persisted == previous);
    assert(!strcmp(Settings.PosixTZ, "PST8PDT,M3.2.0,M11.1.0"));
    assert(Settings.clockFormat == 12 && !strcmp(getenv("TZ"), Settings.PosixTZ));
    nvMem.fail = false; wm.saved(); nvMem.reboot(); applyTimezone();
    assert(!strcmp(Settings.PosixTZ, "EST5EDT,M3.2.0,M11.1.0"));
    assert(Settings.clockFormat == 24);
}
'''


class PortalSettingsSaveTests(unittest.TestCase):
    def test_save_is_durable_before_wifi_or_portal_completion(self):
        source = Path(os.environ.get('SOLOMINER_WMANAGER_SOURCE', ROOT/'src/wManager.cpp')).read_text()
        monitor = (ROOT/'src/monitor.cpp').read_text()
        selection = source[source.index('static bool applyPosixTzSelection'):source.index('// Variables to hold data')]
        notify = source[source.index('void saveConfigCallback()'):source.index('/* void saveParamsCallback()')]
        timezone = monitor[monitor.index('void applyTimezone(void)'):monitor.index('void setup_monitor(void)')]
        if 'auto savePortalParameters = [&]()' in source:
            start = source.index('    bool portalConfigSaved = false;')
            end = source.index('    SERIAL_PRINTLN("AllDone: ");', start)
            registration = source[start:end]
        else:
            # Exercise the previous production registration to prove the test
            # catches the old deferred-save path, without fabricating a failure.
            registration = 'wm.setSaveParamsCallback(saveConfigCallback);\n'
        with tempfile.TemporaryDirectory(prefix='solominer-portal-save-') as temp:
            folder = Path(temp)
            (folder/'test.cpp').write_text(STUBS+selection+notify+timezone+PARAMETERS+registration+CHECKS)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                            '-I'+str(ROOT/'tests/support'), '-I'+str(ROOT/'src'),
                            '-I'+str(ROOT/'.pio/libdeps/Waveshare-AMOLED-206/ArduinoJson/src'),
                            str(folder/'test.cpp'), str(ROOT/'src/settings/settings.cpp'),
                            '-o', str(folder/'test')], check=True)
            subprocess.run([str(folder/'test')], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
