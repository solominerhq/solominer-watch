"""Exercise production wake/reveal ordering with a retained stale frame."""
from pathlib import Path
import os, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
STUBS=r'''
#include <atomic>
static std::atomic<bool> batteryUserWake{false};
#include <cassert>
#include <cstddef>
#define pdTRUE 1
#define portMAX_DELAY 1
#define DISPLAY_WAKE_BRIGHTNESS_DEFAULT 150
#define NAV_CLOCK 0
void* disp_state_mutex=nullptr;
int xSemaphoreTake(void*,int){return 1;}void xSemaphoreGive(void*){}
bool displayOff=true,displayManuallyOff=false,displayAwake=false,inSetupMode=false,chargingBootMode=false;
bool simCountdownActive=false,simCelebrationActive=false,showingLandingSeq=false,showingBlockFoundScreen=false;
int currentScreenIndex=2,forcedScreenIndex=-1,lastBuiltScreen=2;
void endSimulation(){}void dismissBlockFoundScreen(){}
void invalidateScreen(){lastBuiltScreen=-1;}
std::atomic<bool> panelRedrawRequested{false},revealAfterRedraw{false};
unsigned lastWakeTime=0;unsigned millis(){return 10;}
struct {int screenBrightness=150;}Settings;
bool redrawn=false,flushed=false,requested=false;int brightness=0,retries=0;
void setBrightness(int value){if(value)assert(redrawn&&flushed);brightness=value;}
void displayHwWake(){assert(brightness==0);}
void triggerImmediateRedraw(){requested=true;}
namespace network {bool pausePending=false;void requestReconnect(){++retries;}
bool takeSaverPauseEvent(){bool pending=pausePending;pausePending=false;return pending;}}
bool celebrationProtectsPlayback(){return false;}
static void applyBatterySaverScreenReset();
void waveshareDisplay_ScreenDispatcher(unsigned){applyBatterySaverScreenReset();redrawn=true;}
void* lv_scr_act(){return nullptr;}void lv_obj_invalidate(void*){}
void lv_refr_now(void*){assert(redrawn);flushed=true;}
'''
class WakeRedrawTests(unittest.TestCase):
 def test_wake_stays_dark_until_fresh_frame(self):
  text=(ROOT/'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
  a=text.index('static void wakeDisplayInternal(');b=text.index('\nvoid wakeDisplay()',a)
  c=text.index('        if (panelRedrawRequested.exchange(false)) {');d=text.index('        lv_timer_handler();',c)
  e=text.index('static void applyBatterySaverScreenReset() {');f=text.index('\n}\n',e)+3
  checks=r'''
int main(){
 wakeDisplayInternal(false);
 assert(brightness==0 && requested && retries==0 && !displayOff);
 reveal();assert(brightness==150 && flushed && !revealAfterRedraw.load());
 assert(currentScreenIndex==2); // Ordinary display timeout retains Device.
 displayOff=true;displayAwake=false;redrawn=flushed=false;brightness=0;
 wakeDisplayInternal(true);assert(brightness==0&&retries==1);
 // Saver shutdown racing with wake is consumed before the fresh frame appears.
 network::pausePending=true;currentScreenIndex=3;forcedScreenIndex=2;
 reveal();assert(brightness==150&&currentScreenIndex==NAV_CLOCK&&forcedScreenIndex==-1&&lastBuiltScreen==-1);
 displayOff=true;displayAwake=false;redrawn=flushed=false;brightness=0;
 wakeDisplayInternal(true);
 displayManuallyOff=true;reveal();assert(brightness==0); // User-off wins.
}
'''
  with tempfile.TemporaryDirectory() as td:
   p=Path(td);(p/'test.cpp').write_text(STUBS+text[e:f]+text[a:b]+'\nvoid reveal(){\n'+text[c:d]+'}\n'+checks)
   subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
   subprocess.run([str(p/'test')],check=True)
