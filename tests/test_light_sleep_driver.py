"""Compile the production sleep coordinator with hardware boundaries replaced."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
STUBS=r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <vector>
#include "device/sleep_policy.h"
using esp_err_t=int;using gpio_num_t=int;
#define LOW 0
#define HIGH 1
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_TIMEOUT -3
#define ESP_SLEEP_WAKEUP_GPIO 7
#define ESP_SLEEP_WAKEUP_TIMER 4
#define GPIO_INTR_LOW_LEVEL 0
#define GPIO_INTR_HIGH_LEVEL 1
#define GPIO_INTR_NEGEDGE 2
#define BOOT_BUTTON_PIN 0
#define PWR_BUTTON_PIN 10
#define TOUCH_INT_PIN 38
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static uint32_t now=1000;
uint32_t millis(){return now;}
int64_t esp_timer_get_time(){return int64_t(now)*1000;}
void vTaskDelay(int ms){now+=ms;}
static bool idle=true,radio=true,displayOff=true,displayManuallyOff=false,displayInHwSleep=true;
static bool touchInitialized=true,pmuInitialized=true,inSetupMode=false,chargingBootMode=false;
static bool simCountdownActive=false,simCelebrationActive=false,showingLandingSeq=false,showingBlockFoundScreen=false;
static bool touchWakeFlag=false,bootWakeConsumed=false,playing=false,requested=false,ready=true;
static std::atomic<bool> g_serialDisabled{true};
static int bootPin=HIGH,pwrPin=LOW,touchPin=HIGH;
static int sleepCalls=0,activity=0,wakes=0,buttonChecks=0,lvTicks=0,records=0,recordError=0,recordWake=0;
static int configured=0,timerClears=0,gpioClears=0,configFail=-1,sleepError=0,nextCause=ESP_SLEEP_WAKEUP_TIMER;
static uint32_t accountedSleepMs=0;
static int sleepDuration=2000,failLock=0,wakePin=-1;
static bool sleepInputOnRequest=false,audioReady=true,audioHeld=false;
static uint64_t timerUs=0;
static std::vector<int> locks;
static bool touchIsr=true;static int touchMode=GPIO_INTR_NEGEDGE;
void gpio_intr_disable(int pin){assert(pin==TOUCH_INT_PIN);touchIsr=false;}
void gpio_intr_enable(int pin){assert(pin==TOUCH_INT_PIN&&touchMode==GPIO_INTR_NEGEDGE);touchIsr=true;}
void gpio_set_intr_type(int pin,int type){assert(pin==TOUCH_INT_PIN);touchMode=type;}
static int lvgl_mutex=1,disp_state_mutex=2,bus_mutex=3,i2c_mutex=4;
int xSemaphoreTake(int m,int){if(m==failLock)return 0;locks.push_back(m);return pdTRUE;}
void xSemaphoreGive(int m){assert(!locks.empty()&&locks.back()==m);locks.pop_back();}
int digitalRead(int p){return p==BOOT_BUTTON_PIN?bootPin:p==PWR_BUTTON_PIN?pwrPin:touchPin;}
namespace device {
bool batterySaverIdle(){return idle;}
bool requestLightSleep(){assert(!requested);requested=true;if(sleepInputOnRequest)touchWakeFlag=true;return true;}
bool lightSleepReady(){return ready;}
void releaseLightSleep(){assert(locks.empty()&&!audioHeld);requested=false;}
void noteUserActivity(){++activity;idle=false;}
void recordLightSleep(uint32_t duration,int cause,int error){assert(duration==0||duration==uint32_t(sleepDuration));++records;recordError=error;recordWake=cause;}
}
namespace network {bool radioAsleep(){return radio;}}
namespace diagnostics {void prepareForLightSleep(){assert(requested);}}
struct Sample {bool good=true,usb=false;int gauge=80;bool valid()const{return good;}bool usbAbsent()const{return !usb;}};
struct BatterySnapshot {Sample sample;};
static Sample initialBattery,freshBattery;
BatterySnapshot getBatterySnapshot(bool fresh){assert(fresh);return {initialBattery};}
Sample readBatteryRegisters(){assert(locks.size()==4);return freshBattery;}
void flushPendingSettings(bool force){assert(force&&!requested&&locks.empty());}
bool audioIsPlaying(){return playing;}
bool audioPrepareLightSleep(){assert(locks.size()==4);if(!audioReady)return false;audioHeld=true;return true;}
void audioFinishLightSleep(){assert(audioHeld);audioHeld=false;}
int gpio_wakeup_enable(int pin,int level){assert(!touchIsr);if(pin==TOUCH_INT_PIN)touchMode=level;++configured;assert(level==(pin==PWR_BUTTON_PIN?1:0));return configFail==configured?-1:0;}
int esp_sleep_enable_gpio_wakeup(){++configured;return configFail==configured?-1:0;}
int esp_sleep_enable_timer_wakeup(uint64_t duration){timerUs=duration;++configured;return configFail==configured?-1:0;}
void esp_sleep_disable_wakeup_source(int cause){if(cause==ESP_SLEEP_WAKEUP_TIMER)++timerClears;else {assert(cause==ESP_SLEEP_WAKEUP_GPIO);++gpioClears;}}
void gpio_wakeup_disable(int){}
int esp_light_sleep_start(){
 assert(requested&&ready&&audioHeld&&locks.size()==4&&radio&&idle&&timerUs==2000000&&!touchIsr);
 ++sleepCalls;now+=sleepDuration;
 if(wakePin==BOOT_BUTTON_PIN)bootPin=LOW;
 if(wakePin==PWR_BUTTON_PIN)pwrPin=HIGH;
 return sleepError;
}
int esp_sleep_get_wakeup_cause(){return nextCause;}
void lv_tick_inc(uint32_t ms){lvTicks+=ms;}
void checkWaveshareButtons(){assert(locks.empty()&&requested);++buttonChecks;}
void wakeDisplay(){assert(locks.empty());++wakes;displayOff=false;}
'''
CHECKS=r'''
static void reset(){
 now+=61000;idle=radio=displayOff=displayInHwSleep=touchInitialized=pmuInitialized=ready=audioReady=true;
 inSetupMode=chargingBootMode=simCountdownActive=simCelebrationActive=showingLandingSeq=showingBlockFoundScreen=false;
 playing=touchWakeFlag=bootWakeConsumed=sleepInputOnRequest=false;g_serialDisabled=true;
 bootPin=HIGH;pwrPin=LOW;touchPin=HIGH;initialBattery=Sample{};freshBattery=Sample{};
 configured=0;configFail=-1;sleepError=0;nextCause=ESP_SLEEP_WAKEUP_TIMER;sleepDuration=2000;failLock=0;wakePin=-1;
 assert(!requested&&locks.empty()&&!audioHeld&&touchIsr&&touchMode==GPIO_INTR_NEGEDGE);
}
int main(){
 serviceBatterySaverSleep();assert(sleepCalls==1&&activity==0&&wakes==0&&idle&&radio&&lvTicks==2000);
 assert(!requested&&locks.empty()&&!audioHeld&&timerClears==1&&gpioClears==1);
 // Sleep eligibility fails closed; no simulated activity from a timer wake.
 bool* exclusions[]={&idle,&radio,&displayInHwSleep,&touchInitialized,&pmuInitialized};
 for(auto* state:exclusions){reset();*state=false;int before=sleepCalls;serviceBatterySaverSleep();assert(sleepCalls==before);}
 bool* busy[]={&inSetupMode,&chargingBootMode,&simCountdownActive,&simCelebrationActive,&showingLandingSeq,&showingBlockFoundScreen,&playing};
 for(auto* state:busy){reset();*state=true;int before=sleepCalls;serviceBatterySaverSleep();assert(sleepCalls==before);}
 reset();g_serialDisabled=false;int before=sleepCalls;serviceBatterySaverSleep();assert(sleepCalls==before);
 reset();initialBattery.good=false;before=sleepCalls;serviceBatterySaverSleep();assert(sleepCalls==before);
 reset();initialBattery.gauge=2;serviceBatterySaverSleep();assert(sleepCalls==before);
 reset();initialBattery.usb=true;serviceBatterySaverSleep();assert(sleepCalls==before);
 reset();freshBattery.usb=true;serviceBatterySaverSleep();assert(sleepCalls==before&&!requested&&locks.empty());
 reset();ready=false;serviceBatterySaverSleep();assert(sleepCalls==before&&!requested);
 reset();sleepInputOnRequest=true;serviceBatterySaverSleep();assert(sleepCalls==before&&!requested);
 for(int lock=1;lock<=4;++lock){reset();failLock=lock;serviceBatterySaverSleep();assert(sleepCalls==before&&locks.empty()&&!requested);}
 reset();audioReady=false;serviceBatterySaverSleep();assert(sleepCalls==before&&!requested&&locks.empty());
 // Touch wakes even when its pulse is no longer present; wake is real activity.
 reset();nextCause=ESP_SLEEP_WAKEUP_GPIO;serviceBatterySaverSleep();assert(activity==1&&wakes==1&&!idle&&!displayOff);
 reset();wakePin=BOOT_BUTTON_PIN;nextCause=ESP_SLEEP_WAKEUP_GPIO;serviceBatterySaverSleep();assert(bootWakeConsumed&&activity==2&&wakes==2);
 reset();wakePin=PWR_BUTTON_PIN;nextCause=ESP_SLEEP_WAKEUP_GPIO;serviceBatterySaverSleep();assert(activity==3&&wakes==3&&buttonChecks==3);
 // Every partial wake-source configuration failure cleans up and backs off.
 for(int step=1;step<=5;++step){
  reset();configFail=step;int clears=timerClears;before=sleepCalls;serviceBatterySaverSleep();
  assert(recordError==-1&&sleepCalls==before&&timerClears==clears+1&&!requested&&locks.empty());
  configFail=-1;now+=100;serviceBatterySaverSleep();assert(sleepCalls==before);
 }
 reset();sleepError=-9;serviceBatterySaverSleep();assert(recordError==-9);before=sleepCalls;now+=100;serviceBatterySaverSleep();assert(sleepCalls==before);
 reset();sleepDuration=1;serviceBatterySaverSleep();assert(recordError==ESP_ERR_INVALID_STATE);
 reset();nextCause=99;serviceBatterySaverSleep();assert(recordError==ESP_ERR_INVALID_STATE);
 reset();serviceBatterySaverSleep();assert(recordError==0&&recordWake==ESP_SLEEP_WAKEUP_TIMER);
}
'''
class LightSleepDriverTests(unittest.TestCase):
    def test_production_entry_wake_and_failure_cleanup(self):
        source=(ROOT/'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        code=source[source.index('static bool saverSleepEligible()'):source.index('void lv_tick_task(void *arg)')]
        with tempfile.TemporaryDirectory(prefix='solominer-sleep-driver-') as temp:
            cpp=Path(temp)/'sleep.cpp';binary=Path(temp)/'sleep-test'
            cpp.write_text(STUBS+code+CHECKS)
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)
