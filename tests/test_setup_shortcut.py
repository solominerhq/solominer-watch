"""Exercise the real button dispatch and setup restart/USB startup paths."""
from pathlib import Path
import unittest
from test_sleep_wake_buttons import STUBS
from test_reliability_persistence import run

ROOT = Path(__file__).resolve().parents[1]
DISPLAY = ROOT / 'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp'


class SetupShortcut(unittest.TestCase):
    def test_timing_cancellation_boot_hold_and_rollover(self):
        run(r'''
#include <cassert>
#include "ui/setup_shortcut.h"
int main() {
    ui::SetupShortcut s;
    // A chord held across startup is ignored until released.
    assert(s.update(true,true,100).consume);
    assert(!s.update(true,true,6000).armed);
    s.update(false,false,6100);
    assert(!s.update(true,false,6200).consume);
    assert(s.update(true,true,6300).started);
    assert(!s.update(true,true,11299).armed);
    assert(s.update(true,true,11300).armed);
    assert(!s.update(true,true,11400).armed);
    assert(!s.update(true,false,11500).restart);
    assert(!s.update(false,false,11600).restart);
    assert(!s.update(false,false,11649).restart);
    // Release bounce delays restart until BOTH are stably up.
    assert(!s.update(true,false,11650).restart);
    assert(!s.update(false,false,11700).restart);
    assert(s.update(false,false,11750).restart);
    assert(!s.update(false,false,11800).restart);
    // Releasing either button early cancels the whole gesture.
    assert(s.update(true,true,12000).started);
    assert(s.update(false,true,16999).consume);
    assert(!s.update(true,true,18000).armed);
    assert(s.update(false,false,18100).consume);
    assert(!s.update(false,false,18200).consume);
    // Unsigned elapsed time remains correct across millis() rollover.
    ui::SetupShortcut r;
    r.update(false,false,0);
    const uint32_t start=UINT32_MAX-2500;
    r.update(true,true,start);
    assert(!r.update(true,true,start+4999).armed);
    assert(r.update(true,true,start+5000).armed);
    r.update(false,false,start+5100);
    assert(r.update(false,false,start+5150).restart);
}
''')

    def test_production_dispatch_all_screens_and_cancelled_holds(self):
        source = DISPLAY.read_text()
        code = source[source.index('static void pollPwrButton()'):
                      source.index('// Button handler that')]
        run(STUBS+r'''
#include <cstddef>
bool displayAwake=true;
int sleeps=0,wakes=0,shutdowns=0,stops=0,prompts=0,restarts=0,saves=0;
void wakeDisplay(){++wakes;displayOff=displayManuallyOff=false;displayAwake=true;}
void sleepDisplayManually(){++sleeps;displayOff=true;displayAwake=false;}
void powerOffDevice(){++shutdowns;}
void audioStopBlockFound(){++stops;}
int lvgl_mutex=1;const int pdTRUE=1;int pdMS_TO_TICKS(int n){return n;}
int xSemaphoreTake(int,int){return pdTRUE;}void xSemaphoreGive(int){}
void clearPage(){}void invalidateScreen(){}void lv_refr_now(void*){}
const int inter_36=0,inter_24=0,THEME_TEXT_WHITE=0,THEME_TEXT_MUTED=0;
void pageText(const char*,const int*,int,int){}
void showSetupShortcutPrompt(bool failed=false){if(!failed)++prompts;}
void flushPendingSettings(bool force){assert(force);++saves;}
void reboot_to_setup(){assert(boot==HIGH&&pwr==LOW);++restarts;}
'''+code+r'''
int main() {
    checkWaveshareButtons();
    // Mining, Settings, setup, charging and celebration; dark display included.
    for (int mode=0;mode<5;++mode) {
        inSetupMode=mode==2;chargingBootMode=mode==3;
        currentScreenIndex=mode==4?10:mode%4;
        displayOff=mode==0;displayManuallyOff=mode==0;
        now+=500;
        // Stagger the presses: the timer begins only with BOTH down.
        if(mode%2)boot=LOW;else pwr=HIGH;
        checkWaveshareButtons();
        now+=100;boot=LOW;pwr=HIGH;checkWaveshareButtons();
        now+=2999;checkWaveshareButtons();assert(shutdowns==0);
        now+=2000;checkWaveshareButtons();assert(prompts==mode);
        now+=1;checkWaveshareButtons();
        assert(prompts==mode+1&&!bootLongPressFlag&&clicks==0);
        assert(restarts==mode&&shutdowns==0);
        // Releasing one button cannot fire its single-button action.
        if(mode%2)boot=HIGH;else pwr=LOW;
        now+=100;checkWaveshareButtons();assert(restarts==mode&&sleeps==0);
        boot=HIGH;pwr=LOW;now+=50;checkWaveshareButtons();
        now+=49;checkWaveshareButtons();assert(restarts==mode);
        now+=1;checkWaveshareButtons();
        assert(restarts==mode+1&&saves==mode+1&&shutdowns==0&&clicks==0);
        now+=100;checkWaveshareButtons();assert(restarts==mode+1);
    }
    now+=500;boot=LOW;pwr=HIGH;checkWaveshareButtons();
    now+=4900;boot=HIGH;checkWaveshareButtons();
    now+=3000;checkWaveshareButtons();assert(shutdowns==0);
    pwr=LOW;checkWaveshareButtons();assert(restarts==5&&clicks==0&&sleeps==0);
    // Normal controls still work after cancelling.
    now+=500;boot=LOW;checkWaveshareButtons();
    now+=100;boot=HIGH;checkWaveshareButtons();assert(clicks==1);
    now+=500;pwr=HIGH;checkWaveshareButtons();
    now+=100;pwr=LOW;checkWaveshareButtons();assert(sleeps==1);
}
''')

    def test_setup_request_preserves_config_and_handles_write_failure(self):
        source = (ROOT/'src/wManager.cpp').read_text()
        code = source[source.index('#define FORCE_SETUP_FLAG'):
                      source.index('void reset_configuration()')]
        run(r'''
#include <cassert>
#include <string>
#define SERIAL_PRINTLN(...) do{}while(false)
bool exists=false,openOk=true,writeOk=true;int reboots=0,checkpoints=0;
struct File {
    explicit operator bool()const{return openOk;}
    int print(const char*){exists=true;return writeOk?1:0;}
    void close(){}
};
struct Fs {
    File open(const char*,const char*){return {};}
    bool exists(const char*){return ::exists;}
    void remove(const char*){::exists=false;}
} SPIFFS;
struct Esp {void restart(){++reboots;}} ESP;
void checkpointMiningStats(){++checkpoints;}
void delay(int){}
'''+code+r'''
int main(){
    assert(!isSetupBootRequested());
    reboot_to_setup();assert(reboots==1&&checkpoints==1&&isSetupBootRequested());
    // Reading the startup request must not consume it before WiFiManager.
    assert(isSetupBootRequested());assert(checkAndClearForceSetupFlag());
    assert(!isSetupBootRequested()&&!checkAndClearForceSetupFlag());
    openOk=false;reboot_to_setup();assert(reboots==1&&!isSetupBootRequested());
    openOk=true;writeOk=false;reboot_to_setup();assert(reboots==1&&!isSetupBootRequested());
    writeOk=true;reboot_to_setup();assert(reboots==2&&checkpoints==2);
}
''')
        # The portal consumes the marker regardless of missing/valid settings.
        init = source[source.index('void init_WifiManager()'):]
        self.assertIn('bool forceConfig = checkAndClearForceSetupFlag();', init)
        self.assertNotIn('!forceConfig && checkAndClearForceSetupFlag()', init)

    def test_setup_boot_skips_usb_charging_and_splash_and_opens_qr(self):
        source = (ROOT/'src/SoloMinerV2.ino.cpp').read_text()
        charge = source[source.index('  if (isUSBConnected() && !setupBootRequested)'):
                        source.index('\n#endif', source.index('  if (isUSBConnected() && !setupBootRequested)'))]
        status = source[source.index('  if (!setupBootRequested && hasStoredConfig())'):
                        source.index('  doLedStuff(0);', source.index('  if (!setupBootRequested && hasStoredConfig())'))]
        splash = next(line for line in source.splitlines() if 'drawLoadingScreen();' in line)
        run(r'''
#include <cassert>
#include <initializer_list>
#define SERIAL_PRINTLN(...) do{}while(false)
bool usb=false,stored=true,chargingBootMode=false;
int charges=0,splashes=0;
const int NM_Connecting=1,NM_waitingConfig=2;
struct Monitor{int NerdStatus=0;}mMonitor;
bool isUSBConnected(){return usb;}bool hasStoredConfig(){return stored;}
void chargingBootLoop(){++charges;chargingBootMode=false;}
void drawLoadingScreen(){++splashes;}
void boot(bool setupBootRequested) {
'''+charge+splash+status+r'''
}
int main(){
    for(bool connected : {false,true})for(bool hasConfig : {false,true}) {
        usb=connected;stored=hasConfig;charges=splashes=0;boot(true);
        assert(charges==0&&splashes==0&&mMonitor.NerdStatus==NM_waitingConfig);
    }
    usb=true;stored=true;boot(false);assert(charges==1&&splashes==1&&mMonitor.NerdStatus==NM_Connecting);
}
''')
        display = DISPLAY.read_text()
        setup = display[display.index('void waveshareDisplay_SetupScreen(void)'):
                        display.index('// SETUP → CONNECTING SCREEN')]
        self.assertIn('setupShowingDetails = false;', setup)
        self.assertIn('drawSetupQRScreen();', setup)


if __name__ == '__main__':
    unittest.main()
