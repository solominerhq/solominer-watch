"""Compile the production button dispatcher, including charging and setup routes."""
from pathlib import Path
import unittest
from test_sleep_wake_buttons import STUBS
from test_reliability_persistence import run
ROOT=Path(__file__).resolve().parents[1]
class PowerActions(unittest.TestCase):
 def test_tap_powers_off_and_charging_tap_starts(self):
  d=(ROOT/'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
  a=d.index('static void pollPwrButton()');b=d.index('// Button handler that',a)
  code=d[a:b]
  run(STUBS+r'''
#include <cstddef>
bool displayAwake=false;int sleeps=0,wakes=0,shutdowns=0,stops=0;
void wakeDisplay(){++wakes;displayOff=displayManuallyOff=false;displayAwake=true;}
void sleepDisplayManually(){++sleeps;displayManuallyOff=displayOff=true;displayAwake=false;}
void powerOffDevice(){++shutdowns;}
void audioStopBlockFound(){++stops;}
int feedbacks=0;void showPowerOffScreen(){++feedbacks;}
int lvgl_mutex=1;const int pdTRUE=1;int pdMS_TO_TICKS(int n){return n;}
int xSemaphoreTake(int,int){return 0;}void xSemaphoreGive(int){}
void clearPage(){}void invalidateScreen(){}void* lv_scr_act(){return nullptr;}
void lv_refr_now(void*){}
void pageText(const char*,const int*,int,int){}
void showSetupShortcutPrompt(bool=false){}
void flushPendingSettings(bool){}
void reboot_to_setup(){}
'''+code+r'''
int main(){
 // A button held while hardware boots must not shut the fresh boot back down.
 pwr=HIGH;checkWaveshareButtons();now+=4000;checkWaveshareButtons();assert(!shutdowns);
 pwr=LOW;checkWaveshareButtons();assert(!shutdowns&&!wakes);
 checkWaveshareButtons();
 // Normal, setup, and sleeping displays all power off on a tap, never press-down.
 for(int mode=0;mode<3;++mode){inSetupMode=mode==1;displayOff=mode==2;
 now+=500;pwr=HIGH;checkWaveshareButtons();assert(shutdowns==mode);
 now+=100;pwr=LOW;checkWaveshareButtons();assert(shutdowns==mode+1&&stops==mode+1&&feedbacks==mode+1&&!sleeps);
 checkWaveshareButtons();assert(shutdowns==mode+1);}
 // A single tap starts charging mode from either an awake or sleeping screen.
 inSetupMode=false;
 for(int mode=0;mode<2;++mode){chargingBootMode=true;displayOff=mode;displayManuallyOff=mode;displayAwake=!mode;
 now+=500;pwr=HIGH;checkWaveshareButtons();assert(chargingBootMode);
 now+=100;pwr=LOW;checkWaveshareButtons();assert(!chargingBootMode&&!displayOff&&displayAwake&&shutdowns==3&&wakes==mode+1);}
 // Holding retains a single shutdown; release does not start charging mode.
 chargingBootMode=true;
 now+=500;pwr=HIGH;checkWaveshareButtons();now+=2999;checkWaveshareButtons();assert(shutdowns==3);
 now+=1;checkWaveshareButtons();assert(shutdowns==4&&stops==4&&feedbacks==4);
 pwr=LOW;checkWaveshareButtons();assert(shutdowns==4&&chargingBootMode);
}

''')
