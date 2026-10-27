"""Drive the production Wi-Fi callback through display wake and fresh-frame reveal."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include <atomic>
static std::atomic<bool> batteryUserWake{false};
#include <cassert>
#include <cstddef>
#include <cstdio>
#include "WiFi.h"
#include "network/wifi_connection.h"
#include "ui/wifi_display_wake.h"
#include "diagnostics/diagnostics.h"
FakeWiFi WiFi;
uint32_t now=1000;
uint32_t millis(){return now;}
bool saverIdle=false;
namespace device {
bool batterySaverIdle(){return saverIdle;}
bool noteWifiConnected(){return false;}
}
void init_WifiManager(){}
int esp_wifi_start(){return 0;}
int esp_wifi_stop(){return 0;}
namespace diagnostics {void event(Event){}}
bool connectDuringStageRead=false;
namespace network {
WifiStage runtimeWifiStage();
WifiStage wifiStage(){
    const auto observed=runtimeWifiStage();
    // Model the actual asynchronous GOT_IP callback arriving after the UI
    // reads connection state, but before it reads the connection counter.
    if(connectDuringStageRead){connectDuringStageRead=false;WiFi.gotIp();}
    return observed;
}
}
#define pdTRUE 1
#define portMAX_DELAY 1
#define DISPLAY_WAKE_BRIGHTNESS_DEFAULT 150
#define DISPLAY_DIM_BRIGHTNESS 40
#define DIM_TIMEOUT_S 10UL
#define SCREEN_OFF_EXTRA_MS 5000UL
#define WAKE_TIMEOUT_MS (DIM_TIMEOUT_S*1000UL)
#define SCREEN_OFF_TIMEOUT_MS (WAKE_TIMEOUT_MS+SCREEN_OFF_EXTRA_MS)
void* disp_state_mutex=nullptr;
int xSemaphoreTake(void*,int){return 1;}
void xSemaphoreGive(void*){}
bool displayOff=false,displayManuallyOff=false,displayAwake=true;
bool inSetupMode=false,chargingBootMode=false;
std::atomic<bool> panelRedrawRequested{false},revealAfterRedraw{false};
uint32_t lastWakeTime=0;
struct {int screenBrightness=210;} Settings;
int brightness=210,hardwareWakes=0;
bool redrawn=false,flushed=false,requested=false;
void setBrightness(int value){brightness=value;}
void displayHwWake(){assert(brightness==0);++hardwareWakes;}
void displayHwSleep(){brightness=0;}
void triggerImmediateRedraw(){requested=true;}
bool celebrationProtectsPlayback(){return false;}
void waveshareDisplay_ScreenDispatcher(unsigned){redrawn=true;}
void* lv_scr_act(){return nullptr;}
void lv_obj_invalidate(void*){}
void lv_refr_now(void*){assert(redrawn);flushed=true;}
ui::WifiDisplayWake wifiDisplayWake;
'''
CHECKS = r'''
void dark(bool off=true){
    displayOff=off;displayAwake=false;brightness=off?0:40;
    redrawn=flushed=requested=false;
    panelRedrawRequested=false;revealAfterRedraw=false;
}
void prepare(uint32_t time){
    now=time;saverIdle=false;inSetupMode=chargingBootMode=displayManuallyOff=false;
    displayOff=false;displayAwake=true;brightness=Settings.screenBrightness;
    WiFi.state=0;WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,201);
    wifiDisplayWake=ui::WifiDisplayWake{};
    wifiDisplayWake.update(network::successfulConnections(),now,false);
    lastWakeTime=now;
}
void connect(){WiFi.event(ARDUINO_EVENT_WIFI_STA_CONNECTED);WiFi.gotIp();}
void verifyReveal(){
    assert(!displayOff&&displayAwake&&lastWakeTime==now);
    assert(brightness==0&&requested&&!redrawn&&revealAfterRedraw.load());
    reveal();
    assert(redrawn&&flushed&&brightness==Settings.screenBrightness);
    assert(!revealAfterRedraw.load());
}
int main(){
    network::begin();
    // Actual association alone does not wake. Getting an IP does.
    prepare(1000);dark();
    WiFi.event(ARDUINO_EVENT_WIFI_STA_CONNECTED);tickWake();assert(displayOff);
    WiFi.gotIp();tickWake();verifyReveal();
    now+=10001;updateSmartDimming();assert(!displayOff&&!displayAwake&&brightness==40);
    now+=5000;updateSmartDimming();assert(displayOff&&brightness==0);
    // An IP renewal is not a fresh connection and cannot wake again.
    WiFi.gotIp();tickWake();assert(displayOff);
    // Dimmed display wakes through the same fresh-frame path.
    prepare(50000);dark(false);connect();tickWake();verifyReveal();
    // Wi-Fi completing during the UI poll must not permanently lose the wake.
    prepare(100000);dark();connectDuringStageRead=true;
    tickWake();tickWake();
    assert(displayAwake&&!displayOff);
    verifyReveal();
    // Short reconnect flapping remains throttled; a later connection wakes.
    dark();now+=1000;WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,201);
    connect();tickWake();assert(displayOff);
    now+=31000;tickWake();assert(displayOff); // Never replay a suppressed event.
    WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,201);connect();tickWake();verifyReveal();
    // Setup, charging boot, manual-off and expired Saver cannot be woken.
    for(int guard=0;guard<4;++guard){
        prepare(200000+guard*40000);dark();
        inSetupMode=guard==0;chargingBootMode=guard==1;
        displayManuallyOff=guard==2;saverIdle=guard==3;
        connect();tickWake();assert(displayOff);
        inSetupMode=chargingBootMode=displayManuallyOff=saverIdle=false;
        tickWake();assert(displayOff);
    }
    // Already-lit screens keep their existing display timer.
    prepare(500000);lastWakeTime=499000;connect();tickWake();assert(lastWakeTime==499000);
    // A genuinely stopped radio cannot wake the screen, even for a late IP event.
    prepare(600000);dark();network::serviceIdle();assert(network::radioAsleep());
    connect();tickWake();assert(displayOff);
    puts("PASS: Wi-Fi callback -> display wake -> fresh-frame reveal, dim/off timeouts, callback interleaving, cooldown and power guards");
}
'''


class WifiDisplayIntegrationTests(unittest.TestCase):
    def test_callback_to_display(self):
        text = (ROOT / 'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        def section(start, end):
            a = text.index(start)
            return text[a:text.index(end, a)]
        wake = section('static void wakeDisplayInternal(', '\nvoid wakeDisplay()')
        dim = section('void updateSmartDimming() {', '\n// Deferred settings persistence.')
        poll = section('        // Connection notifications wake only the panel;', '\n        // Persist any pending Settings change')
        reveal = section('        if (panelRedrawRequested.exchange(false)) {', '        lv_timer_handler();')
        code = STUBS + wake + dim + '\nvoid tickWake(){\n' + poll + '\n}\nvoid reveal(){\n' + reveal + '\n}\n' + CHECKS
        with tempfile.TemporaryDirectory(prefix='solominer-wifi-display-integration-') as temp:
            p = Path(temp)
            (p / 'test.cpp').write_text(code)
            compiler = os.environ.get('CXX', 'c++')
            flags = ['-std=c++11', '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'tests/network_support'), '-I'+str(ROOT/'src')]
            subprocess.run([compiler, *flags, '-DwifiStage=runtimeWifiStage', '-c', str(ROOT/'src/network/wifi_connection.cpp'), '-o', str(p/'network.o')], check=True)
            subprocess.run([compiler, *flags, str(p/'test.cpp'), str(p/'network.o'), '-o', str(p/'test')], check=True)
            subprocess.run([str(p/'test')], check=True)


if __name__ == '__main__':
    unittest.main()
