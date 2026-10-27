"""Compile the production button dispatcher, including charging and setup routes."""
from pathlib import Path
import unittest
from test_sleep_wake_buttons import STUBS
from test_reliability_persistence import run
ROOT=Path(__file__).resolve().parents[1]
class PowerActions(unittest.TestCase):
 def test_tap_never_shuts_down(self):
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
int lvgl_mutex=1;const int pdTRUE=1;int pdMS_TO_TICKS(int n){return n;}
int xSemaphoreTake(int,int){return 0;}void xSemaphoreGive(int){}
void clearPage(){}void invalidateScreen(){}void* lv_scr_act(){return nullptr;}
void lv_refr_now(void*){}const int inter_36=0,inter_24=0,THEME_TEXT_WHITE=0,THEME_TEXT_MUTED=0;
void pageText(const char*,const int*,int,int){}
void showSetupShortcutPrompt(bool=false){}
void flushPendingSettings(bool){}
void reboot_to_setup(){}
'''+code+r'''
int main(){checkWaveshareButtons();pwr=HIGH;checkWaveshareButtons();assert(wakes==1&&!sleeps&&!shutdowns);
now+=100;pwr=LOW;checkWaveshareButtons();assert(!sleeps&&!shutdowns);
for(int mode=0;mode<2;++mode){inSetupMode=mode==1;
now+=500;pwr=HIGH;checkWaveshareButtons();now+=100;pwr=LOW;checkWaveshareButtons();assert(sleeps==mode+1&&!shutdowns);
now+=500;pwr=HIGH;checkWaveshareButtons();now+=100;pwr=LOW;checkWaveshareButtons();assert(wakes==mode+2&&!shutdowns);}
// Charging: wake consumes its release; a subsequent awake tap starts the watch.
inSetupMode=false;chargingBootMode=true;displayOff=true;displayAwake=false;
now+=500;pwr=HIGH;checkWaveshareButtons();assert(wakes==4&&chargingBootMode&&!displayOff);
now+=100;pwr=LOW;checkWaveshareButtons();assert(chargingBootMode&&sleeps==2&&!shutdowns);
now+=500;pwr=HIGH;checkWaveshareButtons();assert(chargingBootMode);
now+=100;pwr=LOW;checkWaveshareButtons();assert(!chargingBootMode&&!displayOff&&displayAwake&&sleeps==2&&!shutdowns);
// A deliberate charging-screen hold powers off; its release must not start it.
chargingBootMode=true;
now+=500;pwr=HIGH;checkWaveshareButtons();now+=2999;checkWaveshareButtons();assert(!shutdowns&&chargingBootMode);
now+=1;checkWaveshareButtons();assert(shutdowns==1&&stops==1);pwr=LOW;checkWaveshareButtons();assert(shutdowns==1&&chargingBootMode);
}
''')
