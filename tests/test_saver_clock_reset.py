"""Run the production Saver navigation handler without ESP32 display hardware."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include <cassert>
#define NAV_CLOCK 0
bool inSetupMode=false,chargingBootMode=false;
bool simCountdownActive=false,simCelebrationActive=false;
bool showingLandingSeq=false,showingBlockFoundScreen=false;
bool displayOff=true,displayManuallyOff=false;
bool protects=false;bool celebrationProtectsPlayback(){return protects;}
unsigned lastWakeTime=1234,lastActivity=5678;
int currentScreenIndex=2,forcedScreenIndex=3,lastBuiltScreen=2;
int invalidations=0,redraws=0,demosEnded=0,celebrationsEnded=0;
namespace network {
bool pending=false;
bool takeSaverPauseEvent(){bool value=pending;pending=false;return value;}
}
void endSimulation(){++demosEnded;simCountdownActive=simCelebrationActive=false;currentScreenIndex=3;}
void dismissBlockFoundScreen(){++celebrationsEnded;showingLandingSeq=showingBlockFoundScreen=false;}
void invalidateScreen(){++invalidations;lastBuiltScreen=-1;}
void triggerImmediateRedraw(){++redraws;}
'''
CHECKS = r'''
int main(){
 for(int page=0;page<4;++page){
  currentScreenIndex=page;lastBuiltScreen=page;forcedScreenIndex=2;
  applyBatterySaverScreenReset();assert(currentScreenIndex==page&&lastBuiltScreen==page);
  network::pending=true;applyBatterySaverScreenReset();
  assert(currentScreenIndex==NAV_CLOCK&&lastBuiltScreen==-1&&forcedScreenIndex==-1);
  assert(displayOff&&!displayManuallyOff&&lastWakeTime==1234&&lastActivity==5678);
  // After waking/swiping, a consumed pause must not keep snapping back.
  currentScreenIndex=1;applyBatterySaverScreenReset();assert(currentScreenIndex==1);
 }
 assert(invalidations==4&&redraws==4);
 // The reset also works after deliberate screen-off; it must not wake the panel.
 displayManuallyOff=true;network::pending=true;applyBatterySaverScreenReset();
 assert(displayOff&&displayManuallyOff&&currentScreenIndex==NAV_CLOCK);
 // Setup and charging-only mode own their own screens.
 for(auto mode:{&inSetupMode,&chargingBootMode}){
  *mode=true;network::pending=true;currentScreenIndex=3;
  applyBatterySaverScreenReset();assert(currentScreenIndex==3);
  *mode=false;applyBatterySaverScreenReset();assert(currentScreenIndex==3);
 }
 // A demo's return page and celebration flags cannot override Clock at wake.
 simCelebrationActive=true;showingLandingSeq=true;network::pending=true;
 applyBatterySaverScreenReset();
 assert(demosEnded==1&&celebrationsEnded==1&&currentScreenIndex==NAV_CLOCK);
 assert(!simCelebrationActive&&!showingLandingSeq);
 simCountdownActive=true;network::pending=true;applyBatterySaverScreenReset();
 assert(!simCountdownActive&&currentScreenIndex==NAV_CLOCK&&demosEnded==2&&celebrationsEnded==2);
 protects=true;network::pending=true;currentScreenIndex=1;applyBatterySaverScreenReset();
 assert(currentScreenIndex==1);protects=false;applyBatterySaverScreenReset();assert(currentScreenIndex==NAV_CLOCK);
 showingBlockFoundScreen=true;network::pending=true;applyBatterySaverScreenReset();
 assert(!showingBlockFoundScreen&&celebrationsEnded==3&&currentScreenIndex==NAV_CLOCK);
}
'''

class SaverClockResetTests(unittest.TestCase):
    def test_pause_returns_to_clock_once_without_waking(self):
        source = (ROOT/'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        start = source.index('static void applyBatterySaverScreenReset() {')
        end = source.index('\n}\n', start) + 3
        with tempfile.TemporaryDirectory(prefix='solominer-saver-clock-') as temp:
            folder = Path(temp)
            (folder/'test.cpp').write_text('#include <initializer_list>\n'+STUBS+source[start:end]+CHECKS)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
                            str(folder/'test.cpp'), '-o', str(folder/'test')], check=True)
            subprocess.run([str(folder/'test')], check=True, timeout=10)
