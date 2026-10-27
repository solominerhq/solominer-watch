"""Exercise production portal callbacks and Connecting UI across a Wi-Fi change."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#include <cassert>
#include <atomic>
#include <cstring>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include "wManager.h"
using String = std::string;
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mux) (mux)->lock()
#define portEXIT_CRITICAL(mux) (mux)->unlock()
#define SERIAL_PRINTLN(...) ((void)0)
#define SERIAL_PRINTF(...) ((void)0)
struct Server {
    String ssid, password;
    String arg(const char* key) { return strcmp(key,"s")==0 ? ssid : password; }
};
struct Manager {
    std::unique_ptr<Server> server{new Server};
    String stored="Example Home", current="Example Home";
    String getWiFiSSID(bool persistent=true) { return persistent ? stored : current; }
} wm;
unsigned long now=0;
unsigned long millis() { return now; }
struct lv_obj_t {};
lv_obj_t object;
lv_obj_t* lv_scr_act() { return &object; }
void invalidateScreen() {}
namespace ui { enum class SetupView { Connecting }; }
ui::SetupView setupView;
bool setupConnectFailed=false, was_in_setup_mode=false;
int inter_36_bold, inter_28;
#define THEME_TEXT_WHITE 0
#define THEME_TEXT_MUTED 1
#define LV_LABEL_LONG_SCROLL 0
std::vector<String> labels;
void clearPage() { labels.clear(); }
String sanitizeForFont(String text) { return text; }
lv_obj_t* pageText(const char* text,int*,int,int) { labels.push_back(text); return &object; }
void lv_obj_set_height(lv_obj_t*,int) {}
void lv_label_set_long_mode(lv_obj_t*,int) {}
void lv_refr_now(void*) {}
unsigned failures=0;
void drawSetupQRScreen() { ++failures; }
'''

CHECKS = r'''
void submit(const char* ssid,const char* password) {
    wm.server->ssid=ssid;wm.server->password=password;
    // WiFiManager 2.0.17: pre-save, parameter save, then connect/save.
    preSaveConfigCallback();
    saveConfigCallback();
}
int main() {
    setWifiTarget("Example Home");
    submit("Example Phone","example-password");
    assert(shouldSaveConfig && wifiSetupSubmitted && !wifiSetupConnected);
    auto target=wifiTargetSnapshot();
    assert(strcmp(target.ssid,"Example Phone")==0 && target.setupAttempt==1);
    assert(wm.current=="Example Home" && wm.stored=="Example Home");
    serviceConnecting();
    assert(labels.size()==2 && labels[1]=="Example Phone");

    // Even an empty current connection must not fall back to old NVS data.
    wm.current="";saveConfigCallback();
    assert(strcmp(wifiTargetSnapshot().ssid,"Example Phone")==0);

    // Another submission before timeout updates the label and timeout together.
    now=39000;submit("Second network","other-password");serviceConnecting();
    assert(labels[1]=="Second network" && setupConnectingStart==39000);
    now=41000;serviceConnecting();assert(failures==0 && wifiSetupSubmitted);
    // Same-network password corrections also start a fresh attempt.
    now=78000;submit("Second network","corrected-password");serviceConnecting();
    assert(setupConnectingStart==78000 && setupConnectingAttempt==3);
    now=79000;serviceConnecting();assert(failures==0);
    now=118001;serviceConnecting();
    assert(failures==1 && !wifiSetupSubmitted && setupConnectFailed);

    // A retry after timeout displays the new target, with no stale failure state.
    submit("Example Phone","corrected-password");serviceConnecting();
    assert(labels[1]=="Example Phone" && !setupConnectFailed);
    wm.current=wm.stored="Example Phone";saveConfigCallback();
    assert(strcmp(wifiTargetSnapshot().ssid,"Example Phone")==0);

    // No Wi-Fi fields: save other settings without claiming a new connection.
    wifiSetupSubmitted=false;shouldSaveConfig=false;
    auto attempt=wifiTargetSnapshot().setupAttempt;
    submit("","");
    assert(shouldSaveConfig && !wifiSetupSubmitted);
    assert(wifiTargetSnapshot().setupAttempt==attempt);
    // Password-only form uses the saved network, matching WiFiManager.
    wm.stored="Example Home";submit("","new-password");serviceConnecting();
    assert(labels[1]=="Example Home" && wifiSetupSubmitted);
    assert(wifiTargetSnapshot().setupAttempt==attempt+1);

    // SSIDs are not trimmed or HTML-escaped; maximum length stays terminated.
    submit("  Example Phone  ","");serviceConnecting();
    assert(labels[1]=="  Example Phone  ");
    const char* full="12345678901234567890123456789012";
    submit(full,"");serviceConnecting();
    target=wifiTargetSnapshot();
    assert(strlen(target.ssid)==32 && target.ssid[32]=='\0' && labels[1]==full);
    // Invalid empty first-time submission has no saved network to connect to.
    wm.stored="";wifiSetupSubmitted=false;attempt=target.setupAttempt;
    submit("","some-password");
    assert(!wifiSetupSubmitted && wifiTargetSnapshot().setupAttempt==attempt);
    wm.server.reset();preSaveConfigCallback(); // no active HTTP request
    assert(wifiTargetSnapshot().setupAttempt==attempt);
}
'''


class SetupWifiTargetTests(unittest.TestCase):
    def test_submitted_network_survives_callbacks_and_refreshes_display(self):
        source = (ROOT/'src/wManager.cpp').read_text()
        driver = (ROOT/'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        state = source[source.index('// Publish complete snapshots'):source.index('// Trim leading')]
        callbacks = source[source.index('void preSaveConfigCallback()'):source.index('/* void saveParamsCallback()')]
        draw = driver[driver.index('static bool setupConnectingDrawn'):driver.index('// BLOCK FOUND DETECTION')]
        start = driver.index('        if (wifiSetupSubmitted) {')
        end = driver.index('        // Setup mode - show QR code', start)
        service = 'void serviceConnecting() {\n'+driver[start:end]+'}\n'
        with tempfile.TemporaryDirectory(prefix='solominer-setup-target-') as temp:
            folder = Path(temp)
            (folder/'test.cpp').write_text(STUBS+state+callbacks+draw+service+CHECKS)
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror',
                            '-I'+str(ROOT/'src'),str(folder/'test.cpp'),'-o',str(folder/'test')],check=True)
            subprocess.run([str(folder/'test')],check=True,timeout=10)


if __name__ == '__main__':
    unittest.main()
