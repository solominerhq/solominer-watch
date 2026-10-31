"""Persistent celebration controls must accept taps across scene transitions."""
from pathlib import Path
import os, subprocess, tempfile, unittest
ROOT = Path(os.environ.get('SOLOMINER_TEST_ROOT', str(Path(__file__).resolve().parents[1])))
def function(source, signature):
    start = source.index(signature)
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        if source[pos] == '{': depth += 1
        elif source[pos] == '}': depth -= 1
        pos += 1
    return source[start:pos]
class CelebrationInput(unittest.TestCase):
    def test_footer_contacts_survive_scene_changes_but_not_exit(self):
        source = (ROOT/'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        code = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "ui/touch_input.h"
static std::atomic<uint32_t> inputRevision{1};
static int currentScreenIndex=1,lastBuiltScreen=1,setupReturnScreen=3;
static int settingsTappedAction=int(ui::PageAction::None);
static ui::SettingsView settingsView=ui::SettingsView::List;
static ui::SetupView setupView=ui::SetupView::Qr;
static bool inSetupMode=false,chargingBootMode=false,simCountdownActive=false,splashActive=false;
static bool showingLandingSeq=true,showingBlockFoundScreen=false,simCelebrationActive=true;
static bool landingBuilt=true,houstonBuilt=false,certBuilt=false,celebrationHasPlayback=true;
static uint8_t celebrationPage=0;
static uint32_t blockFoundAnimStart=0;
using lv_obj_t=int;
lv_obj_t* lv_scr_act(){static int screen;return &screen;}
void clearPage(){}
uint32_t millis(){return 5000;}
static uint32_t celebrationElapsedMs(){return 5000;}
static uint32_t celebrationHeadingColor(){return 0xffffff;}
namespace ui {namespace block_found {constexpr uint32_t detailsMs=5000;}}
namespace block_found_scene {
lv_obj_t* create(lv_obj_t*,uint32_t(*)(),bool,void(*)(),bool,uint32_t(*)()){return lv_scr_act();}
}
'''
        if 'static void resetScreenBuildState()' in source:
            code += function(source, 'static void resetScreenBuildState()')
        for name in ['void invalidateScreen(void)', 'static void syncInputContext() {',
                     'static void landingDoneCb()', 'static void renderBlockFoundHouston()']:
            code += '\n'+function(source, name)
        code += r'''
static void landing(){
 showingLandingSeq=true;showingBlockFoundScreen=false;celebrationPage=0;
 landingBuilt=true;houstonBuilt=false;invalidateScreen();syncInputContext();
}
int main(){
 for(bool demo:{false,true})for(auto bounds:{ui::demoMute,ui::demoExit}){
  simCelebrationActive=demo;landing();
  const int x=bounds.x+bounds.width/2,y=bounds.y+bounds.height/2;
  ui::TouchGesture finger;ui::TouchQueue queue;ui::TouchEvent out;
  finger.update(true,x,y,100,inputRevision.load(),&bounds);
  landingDoneCb();renderBlockFoundHouston();syncInputContext();
  auto tap=finger.update(false,0,0,180,inputRevision.load());
  assert(tap.kind==ui::TouchKind::Tap && "Mute/close tap lost at astronaut handoff");
  assert(ui::demoAction(tap.x,tap.y)==ui::demoAction(x,y));
  // A completed tap waiting for the display lock survives the same handoff.
  landing();finger.update(true,x,y,300,inputRevision.load(),&bounds);
  queue.push(finger.update(false,0,0,380,inputRevision.load()));
  landingDoneCb();renderBlockFoundHouston();syncInputContext();
  assert(queue.pop(400,inputRevision.load(),out));
  // Changing content pages keeps the controls in the same place and context.
  for(int page:{1,2,0}){
   finger.update(true,x,y,500,inputRevision.load(),&bounds);
   celebrationPage=page;syncInputContext();
   assert(finger.update(false,0,0,580,inputRevision.load()).kind==ui::TouchKind::Tap);
  }
  // A touch cannot leak into the ordinary UI, a countdown, or a new session.
  finger.update(true,x,y,700,inputRevision.load(),&bounds);
  showingBlockFoundScreen=false;simCelebrationActive=false;syncInputContext();
  assert(finger.update(false,0,0,780,inputRevision.load()).kind==ui::TouchKind::None);
  landing();finger.update(true,x,y,800,inputRevision.load(),&bounds);
  invalidateScreen();syncInputContext();
  assert(finger.update(false,0,0,880,inputRevision.load()).kind==ui::TouchKind::None);
  showingLandingSeq=false;simCountdownActive=true;syncInputContext();
  finger.update(true,x,y,900,inputRevision.load(),&bounds);
  simCountdownActive=false;landing();
  assert(finger.update(false,0,0,980,inputRevision.load()).kind==ui::TouchKind::None);
 }
 puts("Celebration footer transition, queued tap and exit isolation checks passed.");
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder); (path/'test.cpp').write_text(code)
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True,timeout=10)
