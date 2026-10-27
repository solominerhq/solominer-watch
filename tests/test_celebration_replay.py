"""Exercise real demo dispatch/clock ordering with a retained prior audio clock."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_celebration_flow import STUBS

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = source.index(name)
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        if source[pos] == '{': depth += 1
        elif source[pos] == '}': depth -= 1
        pos += 1
    return source[start:pos]


class CelebrationReplay(unittest.TestCase):
    def test_previous_song_cannot_advance_new_demo(self):
        source = (ROOT / 'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        stubs = STUBS.replace('static uint32_t elapsed=0;\nuint32_t celebrationElapsedMs(){return elapsed;}',
                              'static uint32_t celebrationElapsedMs();')
        stubs = stubs.replace('void audioStopBlockFound(){playing=false;++stopCalls;}',
                              'static bool stopRequested=false;\nvoid audioStopBlockFound(){stopRequested=true;++stopCalls;}')
        stubs = stubs.replace('void dismissBlockFoundScreen(){clearDemoControls();showingLandingSeq=showingBlockFoundScreen=celebrationHasPlayback=false;audioStopBlockFound();}', '')
        stubs = stubs.replace('void updateCelebrationTour(){}', '')
        stubs = stubs.replace('void renderLandingSequence(){landingBuilt=true;playing=true;}', '')
        stubs += r'''
static uint32_t celebrationStartTick=0,celebrationFallbackStart=0;
static bool celebrationFallbackClock=false;
static uint32_t songPosition=0;
static bool songClockValid=false,sceneFailure=false;
static unsigned playCalls=0,sceneCalls=0;
uint32_t lv_tick_get(){return now;}
uint32_t lv_tick_elaps(uint32_t since){return uint32_t(now-since);}
bool audioBlockFoundPositionMs(uint32_t& value){value=songPosition;return songClockValid;}
void audioPlayBlockFound(){
 if(!hasTrack)return;
 assert(!playing);++playCalls;playing=true;stopRequested=false;
 songClockValid=false;songPosition=0;
}
namespace block_found_scene {
lv_obj_t* create(lv_obj_t*,uint32_t(*clock)(),bool,void(*)(),bool,uint32_t(*)()){
 ++sceneCalls;assert(clock()==0);return sceneFailure?nullptr:lv_scr_act();
}
}
'''
        names = ['void dismissBlockFoundScreen()', 'static void beginSimulationPlayback()',
                 'static void startBlockFoundSimulation(', 'static uint32_t countdownElapsedMs()',
                 'static void renderSimCountdown()', 'static void endSimulation()',
                 'static void exitSimulation()', 'static void closeCelebration()',
                 'static void openSavedBlock()', 'static uint32_t celebrationElapsedMs()',
                 'static uint32_t celebrationHeadingColor()', 'static void updateCelebrationTour()',
                 'static bool celebrationProtectsPlayback() {', 'static void landingDoneCb()',
                 'static void renderLandingSequence()', 'void checkBlockFound()',
                 'static void advanceCelebrationPage(bool forward)', 'static void handleNavigationButton() {',
                 'static void handleGesture(gesture_event_t *event)', 'void updateSmartDimming() {']
        code = stubs + '\n'.join(function(source, name) for name in names)
        start = source.index('void waveshareDisplay_ScreenDispatcher(unsigned long frame) {')
        end = source.index('    // Priority 2: Hold splash', start)
        code += source[start:end].replace('    static bool was_in_setup_mode = false;', '    (void)frame;') + '}\n'
        code += function(source, 'void waveshareDisplay_AnimateScreen(unsigned long frame) {')
        code += r'''
static void dispatch(){waveshareDisplay_ScreenDispatcher(1000);}
int main(){
 assert(!celebrationClockArmed);
 currentScreenIndex=NAV_SETTINGS;valids=3;
 // Replay after exits during landing, reward, QR, or after the full song.
 for(unsigned oldPosition:{0u,1000u,7000u,15000u,130176u})for(bool countIn:{true,false}){
  playing=false;songClockValid=true;songPosition=oldPosition;now+=20000;
  unsigned before=playCalls;
  startBlockFoundSimulation(countIn);
  if(countIn)for(int n=3;n>0;--n){
   waveshareDisplay_AnimateScreen(0);assert(simCountdownActive&&simLastCountdown==n&&playCalls==before);
   now+=1000;
  }
  dispatch();
  assert(showingLandingSeq&&landingBuilt&&!showingBlockFoundScreen&&celebrationPage==0);
  assert(celebrationTour.stage==0&&!celebrationTour.manual&&playCalls==before+1);
  assert(playing&&!songClockValid&&celebrationElapsedMs()==0);
  songClockValid=true;songPosition=900;assert(celebrationElapsedMs()==900);
  dispatch();assert(showingLandingSeq&&celebrationPage==0);
  songPosition=ui::block_found::detailsMs;dispatch();assert(celebrationPage==1);
  now+=ui::block_found::rewardHoldMs;dispatch();assert(celebrationPage==2);
  closeCelebration();assert(!simCelebrationActive&&!showingLandingSeq&&!showingBlockFoundScreen);
  assert(currentScreenIndex==NAV_SETTINGS&&valids==3&&celebrationElapsedMs()==0);
 }
 // A queued audio stop may finish after replay is requested. Until then the
 // old song must not enable dots, navigation or the automatic reward tour.
 playing=true;songClockValid=true;songPosition=130176;
 unsigned before=playCalls;startBlockFoundSimulation(false);dispatch();
 assert(stopRequested&&playCalls==before&&showingLandingSeq&&!landingBuilt);
 assert(celebrationElapsedMs()==0&&celebrationPage==0&&celebrationTour.stage==0);
 advanceCelebrationPage(true);assert(celebrationPage==0&&!celebrationTour.manual);
 now+=500;dispatch();assert(celebrationElapsedMs()==0&&playCalls==before);
 playing=false;dispatch();assert(landingBuilt&&playCalls==before+1&&celebrationElapsedMs()==0);
 // Slow decoder startup stays at frame zero, then follows only the new clock.
 now+=800;assert(celebrationElapsedMs()==0&&!celebrationFallbackClock);
 songClockValid=true;songPosition=400;assert(celebrationElapsedMs()==400);
 closeCelebration();playing=false;
 // Scene allocation failure must still allow music and receipt navigation.
 sceneFailure=true;startBlockFoundSimulation(false);dispatch();
 assert(playing&&showingBlockFoundScreen&&!showingLandingSeq);
 songClockValid=true;songPosition=ui::block_found::detailsMs;dispatch();assert(celebrationPage==1);
 closeCelebration();playing=false;sceneFailure=false;
 // Canceling during the countdown leaves the next complete run clean.
 startBlockFoundSimulation();dispatch();
 gesture_event_t cancel{GESTURE_TAP,200,438};handleGesture(&cancel);before=playCalls;
 assert(!simCountdownActive&&currentScreenIndex==NAV_SETTINGS);
 startBlockFoundSimulation(false);dispatch();assert(playCalls==before+1&&showingLandingSeq&&celebrationPage==0);
 closeCelebration();
 // A real find interrupting a demo must also wait for its own playback clock.
 checkBlockFound();playing=true;songClockValid=true;songPosition=130176;before=playCalls;
 block_record::data.count=++valids;checkBlockFound();dispatch();
 assert(!simCelebrationActive&&showingLandingSeq&&!landingBuilt&&celebrationPage==0);
 assert(playCalls==before&&celebrationElapsedMs()==0);
 playing=false;dispatch();assert(playCalls==before+1&&landingBuilt&&celebrationPage==0);
 closeCelebration();assert(currentScreenIndex==NAV_MINING&&valids==4);
 // Silent builds start immediately, reach the receipt, and replay from zero.
 playing=false;hasTrack=false;songClockValid=true;songPosition=130176;
 for(int repeat=0;repeat<3;++repeat){
  before=playCalls;startBlockFoundSimulation(false);dispatch();
  assert(celebrationElapsedMs()==0&&showingLandingSeq&&!playing&&playCalls==before);
  now+=100;assert(celebrationElapsedMs()==100);
  now+=ui::block_found::detailsMs;dispatch();assert(celebrationPage==1);
  now+=ui::block_found::rewardHoldMs;dispatch();assert(celebrationPage==2);
  closeCelebration();assert(!celebrationClockArmed&&celebrationElapsedMs()==0);
 }
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'test.cpp').write_text(code)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                            '-Werror', '-I'+str(ROOT/'src'), str(path/'test.cpp'),
                            '-o', str(path/'test')], check=True)
            subprocess.run([str(path/'test')], check=True, timeout=10)
