"""Exercise production celebration lifecycle and sleep rules without hardware."""
from pathlib import Path
import os,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
STUBS=r'''
#include <cassert>
#include <initializer_list>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "ui/page_controls.h"
#include "ui/block_found_sequence.h"
#include "device/block_record.h"
#define NAV_CLOCK 0
#define NAV_MINING 1
#define NAV_SETTINGS 3
using lv_obj_t=int;
static bool splashActive=false;
static uint32_t elapsed=0;
uint32_t celebrationElapsedMs(){return elapsed;}
static unsigned now=1000,lastWakeTime=0,blockFoundAnimStart=0,simCountdownStart=0;
unsigned millis(){return now;}
static bool displayOff=false,displayManuallyOff=false,displayAwake=true,inSetupMode=false,chargingBootMode=false;
static bool simCountdownActive=false,simCelebrationActive=false,showingLandingSeq=false,showingBlockFoundScreen=false;
static bool celebrationHasPlayback=false,landingBuilt=false,houstonBuilt=false,certBuilt=false;
static bool celebrationClockArmed=false;
static bool playing=false,muted=false,controls=false;
static unsigned stopCalls=0,wakeCalls=0,invalidations=0,redraws=0;
static int currentScreenIndex=NAV_CLOCK,simReturnScreen=0,lastBlocksFound=0,valids=0,simLastCountdown=-1;
static unsigned char celebrationPage=0;
static ui::block_found::Tour celebrationTour;
static lv_obj_t *simCountdownLabel=nullptr,*g_landingImg=nullptr;
static std::atomic<bool> navigationClickPending{false};
static bool bootLongPressFlag=false;
static block_record::Record celebrationRecord;
static ui::SetupView setupView=ui::SetupView::Qr;
static ui::SettingsView settingsView=ui::SettingsView::List;
static int settingsTappedAction=int(ui::PageAction::None);
static ui::SetupAction setupRequested=ui::SetupAction::None;
void handleSetupAction(ui::SetupAction action) {setupRequested=action;}
#define NUM_MAIN_SCREENS 4
#define WL_CONNECTED 3
#define LV_SYMBOL_MUTE "mute"
#define LV_SYMBOL_VOLUME_MAX "volume"
struct {int status(){return WL_CONNECTED;}} WiFi;
static bool showingMiningStats=false;
static int lastBuiltScreen=NAV_SETTINGS;
static lv_obj_t* demoMuteLabel=nullptr;
bool audioBlockFoundMuted(){return muted;}
void lv_label_set_text(lv_obj_t*,const char*){} void lv_obj_center(lv_obj_t*){}
namespace network {void requestReconnect(){}}
namespace device {void noteUserActivity(){}}
enum Gesture {GESTURE_TAP,GESTURE_SWIPE_LEFT,GESTURE_SWIPE_RIGHT};
struct gesture_event_t {Gesture type;int start_x;int start_y;};
namespace block_record {static Record data;Record snapshot(){return data;}void review(uint32_t count){if(data.count==count)data.unread=false;}}
namespace diagnostics {enum class Event{DemoStart,DemoEnd,DemoCancel};void event(Event){}}

void clearDemoControls(){controls=false;}
void audioPrewarm(){}
void audioSetBlockFoundMuted(bool m){muted=m;}
void audioStopBlockFound(){playing=false;++stopCalls;}
bool audioIsPlaying(){return playing;}
static bool hasTrack=true;
bool audioHasBlockFoundTrack(){return hasTrack;}
void wakeDisplay(){displayOff=displayManuallyOff=false;displayAwake=true;lastWakeTime=now;++wakeCalls;}
void triggerImmediateRedraw(){++redraws;}
void invalidateScreen(){++invalidations;}
void toggleSetupScreen(){}
void waveshareDisplayToggle(){currentScreenIndex=(currentScreenIndex+1)%4;}
void dismissBlockFoundScreen(){clearDemoControls();showingLandingSeq=showingBlockFoundScreen=celebrationHasPlayback=false;audioStopBlockFound();}
#define SIM_COUNTDOWN_SECS 3
void clearPage(){}
lv_obj_t* lv_scr_act(){static lv_obj_t obj;return &obj;}
void buildDemoControls(){controls=true;}
static int inter_28=28,inter_120=120;
#define THEME_TEXT_WHITE 1
lv_obj_t* pageText(const char*,const int*,int,int){return lv_scr_act();}
lv_obj_t* pageAction(const ui::PageRect&,const char*){static lv_obj_t button;return &button;}
void lv_obj_set_style_radius(lv_obj_t*,int,int){}
void lv_label_set_text_fmt(lv_obj_t*,const char*,int){}
#define SCREEN_OFF_TIMEOUT_MS 15000
#define WAKE_TIMEOUT_MS 10000
#define DISPLAY_DIM_BRIGHTNESS 20
#define pdTRUE 1
#define portMAX_DELAY 1
static void* disp_state_mutex=nullptr;
int xSemaphoreTake(void*,int){return 1;}void xSemaphoreGive(void*){}
static int brightness=150;void setBrightness(int value){brightness=value;}void displayHwSleep(){}
#define DISPLAY_WAKE_BRIGHTNESS_DEFAULT 150
static unsigned splashStartMs=0;
#define SPLASH_MIN_MS 1500
static std::atomic<bool> panelRedrawRequested{false},revealAfterRedraw{false};
struct {int screenBrightness=150;}Settings;
struct InputContextAfterDraw { ~InputContextAfterDraw(){} };
void serviceBatteryNotice() {}
void applyBatterySaverScreenReset(){}
void updateCelebrationTour(){}void updateDemoControls(){}
void renderLandingSequence(){landingBuilt=true;playing=true;}
void renderCertificate(){}void renderBlockFoundHouston(){}
void lv_obj_invalidate(lv_obj_t*){}void lv_refr_now(void*){}void lv_timer_handler(){}

'''
CHECKS=r'''
int main(){
 // Upper/BOOT can wake charging, but must never start the watch from that view.
 chargingBootMode=true;displayOff=true;displayAwake=false;
 handleNavigationButton();assert(chargingBootMode&&!displayOff&&displayAwake);
 handleNavigationButton();assert(chargingBootMode&&!displayOff&&currentScreenIndex==NAV_CLOCK);
 chargingBootMode=false;
 strcpy(block_record::data.hash,"0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5");
 block_record::data.height=840000;block_record::data.count=4;block_record::data.unread=true;
 valids=4;checkBlockFound();assert(!showingLandingSeq); // Boot loads without replay.
 currentScreenIndex=3;muted=true;startBlockFoundSimulation();
 assert(simCountdownActive && !simCelebrationActive && !showingLandingSeq && !muted);
 // Deliberately omit mining-data refreshes: the animation loop owns the deadline.
 for(int n=SIM_COUNTDOWN_SECS;n>0;--n){waveshareDisplay_AnimateScreen(0);assert(simLastCountdown==n&&!simCelebrationActive);now+=1000;}
 waveshareDisplay_AnimateScreen(0);
 assert(simCelebrationActive&&!simCountdownActive&&showingLandingSeq&&celebrationPage==0&&playing&&landingBuilt);
 assert(celebrationProtectsPlayback());now+=30000;updateSmartDimming();assert(!displayOff&&lastWakeTime==now);
 showingLandingSeq=false;showingBlockFoundScreen=true;playing=true;
 now+=30000;updateSmartDimming();assert(!displayOff);
 playing=false;assert(!celebrationProtectsPlayback());
 now+=10001;updateSmartDimming();assert(!displayAwake&&!displayOff);
 now+=5000;updateSmartDimming();assert(displayOff);
 handleNavigationButton();assert(!displayOff&&simCelebrationActive); // Wake only.
 for(int page : {1,2,0}) {
  handleNavigationButton();assert(simCelebrationActive&&celebrationPage==page&&currentScreenIndex==3&&!playing);
 }
 closeCelebration();assert(!simCelebrationActive&&currentScreenIndex==3);
 assert(valids==4&&block_record::data.unread);
 startBlockFoundSimulation();muted=true;playing=true;controls=true;displayOff=true;displayAwake=false;
 valids=5;block_record::data.count=5;navigationClickPending=true;bootLongPressFlag=true;
 checkBlockFound();assert(!simCelebrationActive&&showingLandingSeq&&!displayOff&&muted);
 assert(celebrationRecord.count==5&&!navigationClickPending&&!bootLongPressFlag&&!controls);
 elapsed=ui::block_found::dotsMs-1;handleNavigationButton();
 assert(showingLandingSeq&&celebrationPage==0&&block_record::data.unread);
 elapsed=ui::block_found::dotsMs;handleNavigationButton();
 assert(!showingLandingSeq&&showingBlockFoundScreen&&celebrationPage==1&&!block_record::data.unread);
 for(int page : {2,0,1}) {handleNavigationButton();assert(celebrationPage==page&&showingBlockFoundScreen);}
 closeCelebration();assert(currentScreenIndex==NAV_MINING);
 openSavedBlock();assert(showingBlockFoundScreen&&!showingLandingSeq&&!celebrationHasPlayback&&!playing);
 assert(celebrationPage==0&&celebrationRecord.count==5);
 // A newly saved record must not be marked read by closing an older receipt.
 block_record::data.count=6;block_record::data.unread=true;closeCelebration();assert(block_record::data.unread);
 // Settings subpages use BOOT as Back; the main list still cycles normally.
 for(auto detail : {ui::SettingsView::Brightness,ui::SettingsView::BatterySaver,ui::SettingsView::SetupConfirmation}) {
  currentScreenIndex=NAV_SETTINGS;settingsView=detail;settingsTappedAction=int(ui::PageAction::None);
  handleNavigationButton();assert(settingsTappedAction==int(ui::PageAction::Back)&&currentScreenIndex==NAV_SETTINGS);
 }
 settingsView=ui::SettingsView::List;settingsTappedAction=int(ui::PageAction::None);
 handleNavigationButton();assert(currentScreenIndex==NAV_CLOCK&&settingsTappedAction==int(ui::PageAction::None));
 // Swipes across a Settings row must navigate, never activate that row.
 currentScreenIndex=NAV_SETTINGS;settingsView=ui::SettingsView::List;
 gesture_event_t swipe{GESTURE_SWIPE_LEFT,100,250};handleGesture(&swipe);
 assert(currentScreenIndex==NAV_CLOCK&&settingsTappedAction==int(ui::PageAction::None));
 for(auto detail : {ui::SettingsView::Brightness,ui::SettingsView::BatterySaver,ui::SettingsView::SetupConfirmation}) {
  currentScreenIndex=NAV_SETTINGS;settingsView=detail;settingsTappedAction=int(ui::PageAction::None);
  swipe.type=GESTURE_SWIPE_LEFT;handleGesture(&swipe);
  assert(currentScreenIndex==NAV_SETTINGS&&settingsTappedAction==int(ui::PageAction::None));
  swipe.type=GESTURE_SWIPE_RIGHT;handleGesture(&swipe);
  assert(currentScreenIndex==NAV_SETTINGS&&settingsTappedAction==int(ui::PageAction::Back));
 }
 settingsView=ui::SettingsView::List;settingsTappedAction=int(ui::PageAction::None);
 gesture_event_t tap{GESTURE_TAP,100,250};handleGesture(&tap);
 assert(settingsTappedAction==int(ui::PageAction::BatterySaver));
 settingsTappedAction=int(ui::PageAction::None);displayAwake=false;handleGesture(&tap);
 assert(displayAwake&&settingsTappedAction==int(ui::PageAction::None));
 inSetupMode=true;setupView=ui::SetupView::Manual;handleGesture(&swipe);
 assert(setupRequested==ui::SetupAction::Qr);
 setupView=ui::SetupView::Qr;tap={GESTURE_TAP,280,440};handleGesture(&tap);
 assert(setupRequested==ui::SetupAction::Exit);
 inSetupMode=false;setupView=ui::SetupView::Qr;
 // Cancel during the count-in returns to the original page without starting audio.
 currentScreenIndex=NAV_SETTINGS;startBlockFoundSimulation();renderSimCountdown();
 assert(simCountdownActive&&!playing);
 // The old mute position now belongs to the wide Cancel button.
 tap={GESTURE_TAP,200,438};handleGesture(&tap);
 assert(!simCountdownActive&&!simCelebrationActive&&!playing&&currentScreenIndex==NAV_SETTINGS);
 startBlockFoundSimulation();renderSimCountdown();
 tap={GESTURE_TAP,200,250};handleGesture(&tap);assert(simCountdownActive);
 handleNavigationButton();assert(!simCountdownActive&&!simCelebrationActive&&!playing);
 // Unsigned elapsed math still completes across millis rollover.
 now=0xfffffc00u;startBlockFoundSimulation();renderSimCountdown();
 assert(simLastCountdown==3);now+=1000;renderSimCountdown();assert(simLastCountdown==2);
 now+=1000;renderSimCountdown();assert(simLastCountdown==1);
 now+=1000;renderSimCountdown();assert(!simCountdownActive&&showingLandingSeq);closeCelebration();
}
'''
class CelebrationFlow(unittest.TestCase):
 def test_lifecycle_sleep_and_settings_navigation(self):
  s=(ROOT/'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
  names=['static void beginSimulationPlayback()', 'static void startBlockFoundSimulation(',
   'static uint32_t countdownElapsedMs()', 'static void renderSimCountdown()', 'static void endSimulation()', 'static void exitSimulation()', 'static void closeCelebration()',
   'static void openSavedBlock()', 'static bool celebrationProtectsPlayback() {',
   'void checkBlockFound()', 'static void advanceCelebrationPage(bool forward)', 'static void handleNavigationButton() {', 'static void handleGesture(gesture_event_t *event)', 'void updateSmartDimming() {']
  functions=[]
  for name in names:
   a=s.index(name);b=s.index('{',a);depth=1;i=b+1
   while depth:
    if s[i]=='{':depth+=1
    elif s[i]=='}':depth-=1
    i+=1
   functions.append(s[a:i])
  # Exercise the production celebration dispatcher branch without unrelated
  # clock/setup dependencies; its once-per-second caller is deliberately absent.
  a=s.index('void waveshareDisplay_ScreenDispatcher(unsigned long frame) {')
  b=s.index('    // Priority 2: Hold splash',a)
  dispatcher=s[a:b].replace('    static bool was_in_setup_mode = false;','    (void)frame;')+'}\n'
  a=s.index('void waveshareDisplay_AnimateScreen(unsigned long frame) {')
  b=s.index('void waveshareDisplay_DoLedStuff',a)
  functions += [dispatcher,s[a:b]]
  with tempfile.TemporaryDirectory() as td:
   folder=Path(td);cpp=folder/'test.cpp';binary=folder/'test'
   cpp.write_text(STUBS+'\n'.join(functions)+CHECKS)
   subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True)
   subprocess.run([str(binary)],check=True)
