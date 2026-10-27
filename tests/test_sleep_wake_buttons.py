"""Actual button recognizers: a sleep-wake press cannot also shut down/change page."""
from pathlib import Path
import os,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
STUBS=r'''
#include <cassert>
#include <cstdint>
#include <atomic>
#include "ui/setup_shortcut.h"
#define LOW 0
#define HIGH 1
#define BOOT_BUTTON_PIN 0
#define PWR_BUTTON_PIN 10
#define PWR_OFF_HOLD_MS 3000
#define NAV_CLOCK 0
#define NAV_SETTINGS 3
#define SERIAL_PRINTLN(...) do{}while(false)
#define SERIAL_PRINTF(...) do{}while(false)
static uint32_t now=1000,lastWakeTime=0;
uint32_t millis(){return now;}
static int boot=HIGH,pwr=LOW,clicks=0,currentScreenIndex=0;
int digitalRead(int pin){return pin==BOOT_BUTTON_PIN?boot:pwr;}
static bool displayOff=true,displayManuallyOff=false,pwrShortFlag=false,pwrLongFlag=false;
static bool bootWakeConsumed=false,bootLongPressFlag=false,inSetupMode=false,chargingBootMode=false;
std::atomic<bool> setupButtonsConsumed{false},navigationClickPending{false};
ui::SetupShortcut setupShortcut;
namespace device {void noteUserActivity(){}}
void waveshareButtonClick(){++clicks;}
'''
CHECKS=r'''
int main(){
 pollPwrButton();pollBootButton();
 pwr=HIGH;pollPwrButton();assert(pwrShortFlag);pwrShortFlag=false;displayOff=false;
 now+=100;pwr=LOW;pollPwrButton();assert(!pwrShortFlag&&!pwrLongFlag);
 // Same protection if the user explicitly turned the screen off before sleep.
 displayManuallyOff=true;now+=500;pwr=HIGH;pollPwrButton();assert(pwrShortFlag);
 pwrShortFlag=false;displayManuallyOff=false;now+=100;pwr=LOW;pollPwrButton();assert(!pwrShortFlag);
 // BOOT wakes on press; its release is consumed, not a page-change action.
 now+=500;boot=LOW;pollBootButton();bootWakeConsumed=true;now+=100;boot=HIGH;pollBootButton();assert(clicks==0&&!bootWakeConsumed);
 // Subsequent ordinary press still works, and a deliberate long hold still works.
 now+=500;boot=LOW;pollBootButton();now+=100;boot=HIGH;pollBootButton();assert(clicks==1);
 now+=500;boot=LOW;pollBootButton();bootWakeConsumed=true;now+=5000;pollBootButton();assert(bootLongPressFlag);
 boot=HIGH;pollBootButton();assert(clicks==1&&!bootWakeConsumed);
 // Awake tap requests screen sleep; a long hold requests power-off once.
 now+=500;pwr=HIGH;pollPwrButton();assert(!pwrShortFlag);now+=100;pwr=LOW;pollPwrButton();assert(pwrShortFlag);pwrShortFlag=false;
 now+=500;pwr=HIGH;pollPwrButton();now+=2999;pollPwrButton();assert(!pwrLongFlag);
 now+=1;pollPwrButton();assert(pwrLongFlag);pwrLongFlag=false;
 now+=500;pollPwrButton();assert(!pwrLongFlag);pwr=LOW;pollPwrButton();assert(!pwrShortFlag);
}
'''
class SleepWakeButtons(unittest.TestCase):
    def test_wake_press_consumption(self):
        s=(ROOT/'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        a=s.index('static void pollPwrButton()');b=s.index('// EXPORTED FUNCTIONS',a)
        code=s[a:b]
        with tempfile.TemporaryDirectory(prefix='solominer-wake-buttons-') as tmp:
            cpp=Path(tmp)/'buttons.cpp';binary=Path(tmp)/'buttons'
            cpp.write_text(STUBS+code+CHECKS)
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)
