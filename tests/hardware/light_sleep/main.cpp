// Standalone hardware qualification; built only by platformio.sleep-test.ini.
// No mining, pool traffic, customer setup writes, LVGL tasks, audio or background
// filesystem activity. Qualifies hardware wake/rejoin, not production integration.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <XPowersLib.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include "diagnostics/record.h"

namespace {
constexpr int Touch=38, Pwr=10, Boot=0;
constexpr uint32_t JoinMs=20000, DwellMs=5000, FileLimit=128*1024;
constexpr const char* Log="/light-sleep-test.jsonl";
Arduino_ESP32QSPI bus(12,11,4,5,6,7);
Arduino_CO5300 panel(&bus,8,0,410,502,22,0,0,0);
XPowersAXP2101 pmu;
bool pmuOk=false, screenOk=false, screenAsleep=false, storageOk=false, serialOn=false;
uint32_t bootId=0, runId=0, storageErrors=0;
char ssid[33]={},password[65]={};
bool haveWifi=false, inputReady=false;
uint64_t nowMs(){return uint64_t(esp_timer_get_time())/1000;}
// Reject an unreadable PMU bus before interpreting absent VBUS as battery power.
bool powerKnown(){ Wire.beginTransmission(AXP2101_SLAVE_ADDRESS);return pmuOk&&Wire.endTransmission()==0; }
bool usb(){return !powerKnown() || pmu.isVbusIn();}
int percent(){return pmuOk?pmu.getBatteryPercent():-1;}
int voltage(){return pmuOk?pmu.getBattVoltage():-1;}
void serialPower(){bool want=usb();if(want&&!serialOn){Serial.begin(115200);serialOn=true;}else if(!want&&serialOn){Serial.end();serialOn=false;}}
void screen(const char* a,const char* b="",const char* c="",const char* d=""){
 if(!screenOk)return;
 if(screenAsleep){panel.displayOn();screenAsleep=false;}
 panel.setBrightness(100);panel.fillScreen(0);panel.setTextColor(0xFFFF);panel.setTextSize(4);panel.setCursor(28,60);panel.print("SLEEP TEST");
 panel.setTextSize(3);const char* lines[]={a,b,c,d};for(int i=0;i<4;++i){panel.setCursor(28,145+i*55);panel.print(lines[i]);}
}
void dark(){if(screenOk&&!screenAsleep){panel.setBrightness(0);panel.displayOff();screenAsleep=true;}}
// A full touch read acknowledges the controller's interrupt. Used while awake only.
bool touched(){Wire.beginTransmission(0x38);Wire.write(0x02);if(Wire.endTransmission()!=0)return false;Wire.requestFrom(0x38,5);if(Wire.available()<5)return false;uint8_t n=Wire.read()&15,x=Wire.read();while(Wire.available())Wire.read();return n>0&&n<=2&&((x>>6)&3)!=1&&((x>>6)&3)!=3;}
bool input(){return digitalRead(Boot)==LOW||digitalRead(Pwr)==HIGH||touched();}
uint32_t pinMask(){return (digitalRead(Boot)==LOW?1u:0u)|(digitalRead(Pwr)==HIGH?2u:0u)|(digitalRead(Touch)==LOW?4u:0u);}
bool record(const char* event,int cycle=-1,uint32_t planned=0,uint64_t slept=0,int cause=0,int error=0,uint32_t join=0,int rssi=-127,uint32_t pins=0){
 if(!storageOk)return false;
 char line[640];int n=snprintf(line,sizeof(line),"{\"t\":\"probe\",\"schema\":1,\"fw\":\"light-sleep-probe-1\",\"b\":%u,\"run\":%u,\"ms\":%llu,\"event\":\"%s\",\"cycle\":%d,\"planned_ms\":%u,\"slept_ms\":%llu,\"wake\":%d,\"error\":%d,\"join_ms\":%u,\"rssi\":%d,\"pins\":%u,\"usb\":%d,\"raw\":%d,\"mv\":%d,\"reset\":%d,\"heap\":%u}\n",bootId,runId,nowMs(),event,cycle,planned,slept,cause,error,join,rssi,pins,usb()?1:0,percent(),voltage(),int(esp_reset_reason()),ESP.getFreeHeap());
 if(n<=0||size_t(n)>=sizeof(line)){++storageErrors;return false;}
 File f=SPIFFS.open(Log,FILE_APPEND);
 bool room=f&&f.size()+size_t(n)<=FileLimit&&diagnostics::canAppend(SPIFFS.totalBytes(),SPIFFS.usedBytes(),size_t(n));
 bool ok=room&&f.write(reinterpret_cast<uint8_t*>(line),size_t(n))==size_t(n);if(f){f.flush();f.close();}
 if(!ok){++storageErrors;storageOk=false;}return ok;
}
void wifiOff(){WiFi.disconnect(true,false);}
bool batteryReady(){int p=percent(),v=voltage();return powerKnown()&&!usb()&&p>10&&p<=100&&v>=3500;}
void finish(const char* event,const char* reason){wifiOff();record(event);serialPower();screen(reason,"Plug in to export","BOOT: run again");inputReady=false;}
// Returns 0=joined, 1=timeout, 2=USB/user, 3=low/unknown battery.
int join(uint32_t& elapsed){
 uint32_t start=millis();WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(false);WiFi.setSleep(WIFI_PS_MIN_MODEM);WiFi.begin(ssid,password);
 while(WiFi.status()!=WL_CONNECTED){
  if(usb()||input()){elapsed=millis()-start;return 2;}
  if(!batteryReady()){elapsed=millis()-start;return 3;}
  if(uint32_t(millis()-start)>=JoinMs){elapsed=millis()-start;return 1;}delay(100);
 }
 elapsed=millis()-start;
 uint32_t held=millis();while(uint32_t(millis()-held)<DwellMs){if(usb()||input())return 2;if(WiFi.status()!=WL_CONNECTED)return 1;delay(50);}return 0;
}
void run(){
 if(!haveWifi){screen("No saved Wi-Fi","Restore normal app","and set up Wi-Fi");return;}
 if(!storageOk){screen("Log unavailable","Plug in to export","Do not erase flash");return;}
 if(!batteryReady()){screen("Unplug USB first","Battery above 10%","BOOT: try again");return;}
 runId=esp_random();if(!record("start"))return;
 screen("Starting in 5 sec","Then screen sleeps","Touch/PWR: end test");
 uint32_t start=millis();while(uint32_t(millis()-start)<5000){if(usb()){finish("usb_stop","USB: test stopped");return;}touched();delay(20);}
 if(input()){finish("input_held","Release buttons");return;}
 uint32_t joined=0;int status=join(joined);
 if(status){record("initial_join_failed",-1,0,0,0,status,joined);finish("stopped","Initial join failed");return;}
 if(!record("initial_join",-1,0,0,0,0,joined,WiFi.RSSI())){finish("log_failed","Log write failed");return;}
 for(int cycle=0;cycle<22;++cycle){
  uint32_t duration=cycle==0?10000u:cycle==1?60000u:180000u;
  if(!batteryReady()){finish("power_stop","Power: test stopped");return;}
  wifiOff();
  if(WiFi.getMode()!=WIFI_MODE_NULL){finish("radio_stop_failed","Radio stop failed");return;}
  dark();touched();delay(100);
  if(pinMask()){record("pin_asserted",cycle,duration,0,0,0,0,-127,pinMask());finish("stopped","Wake input held");return;}
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_err_t e=gpio_wakeup_enable(gpio_num_t(Boot),GPIO_INTR_LOW_LEVEL);
  if(e==ESP_OK)e=gpio_wakeup_enable(gpio_num_t(Pwr),GPIO_INTR_HIGH_LEVEL);
  if(e==ESP_OK)e=gpio_wakeup_enable(gpio_num_t(Touch),GPIO_INTR_LOW_LEVEL);
  if(e==ESP_OK)e=esp_sleep_enable_gpio_wakeup();
  if(e==ESP_OK)e=esp_sleep_enable_timer_wakeup(uint64_t(duration)*1000);
  if(e!=ESP_OK){record("wake_config_failed",cycle,duration,0,0,e);finish("stopped","Wake setup failed");return;}
  if(!record("sleep_enter",cycle,duration)){finish("log_failed","Log write failed");return;}
  // No application task owns I2C/QSPI/SPIFFS here; writes are synchronous/closed.
  // Retain default flash/PSRAM power policy. No PM framework or tickless required
  // for this explicit sleep test. No mining task watchdogs are installed.
  if(serialOn){Serial.end();serialOn=false;}
  uint64_t before=nowMs();e=esp_light_sleep_start();uint64_t slept=nowMs()-before;
  int cause=int(esp_sleep_get_wakeup_cause());uint32_t pins=pinMask();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(gpio_num_t(Boot));gpio_wakeup_disable(gpio_num_t(Pwr));gpio_wakeup_disable(gpio_num_t(Touch));
  if(!record("sleep_exit",cycle,duration,slept,cause,e,0,-127,pins)){finish("log_failed","Log write failed");return;}
  serialPower();
  if(e!=ESP_OK){finish("sleep_failed","Sleep rejected");return;}
  if(usb()){finish("usb_stop","USB: test stopped");return;}
  if(cause==ESP_SLEEP_WAKEUP_GPIO){finish("user_wake","Input woke watch");return;}
  // Unexpected wake or short return stops the run instead of hammering Wi-Fi.
  if(cause!=ESP_SLEEP_WAKEUP_TIMER||slept+500<duration||slept>duration+2000){finish("unexpected_wake","Check wake log");return;}
  if(!batteryReady()){finish("power_stop","Power: test stopped");return;}
  status=join(joined);
  if(status){record("rejoin_failed",cycle,duration,0,0,status,joined);finish("stopped","Reconnect stopped");return;}
  if(!record("rejoined",cycle,duration,0,0,0,joined,WiFi.RSSI())){finish("log_failed","Log write failed");return;}
 }
 finish("complete","22 cycles complete");
}
// Same checked framing as the production exporter; this whitelists only the
// probe log. Bounded writes prevent an absent host blocking the watch forever.
bool writeHost(const char* p,size_t n){uint32_t last=millis();while(n){int room=Serial.availableForWrite();if(room>0){size_t sent=Serial.write(reinterpret_cast<const uint8_t*>(p),min(n,size_t(room)));p+=sent;n-=sent;if(sent)last=millis();}if(uint32_t(millis()-last)>2000)return false;delay(1);}return true;}
void exportLog(){
 if(!serialOn)return;File f=SPIFFS.open(Log,FILE_READ);if(!f)return;
 if(!writeHost("\nSM_DIAG_BEGIN 1\n",17)){f.close();return;}
 uint8_t data[128];uint32_t crc=0xFFFFFFFFu,total=0;char line[300];
 while(f.available()){size_t n=f.read(data,sizeof(data));if(!n){f.close();return;}crc=diagnostics::crcUpdate(crc,data,n);total+=n;memcpy(line,"SM_DIAG_DATA ",13);for(size_t i=0;i<n;++i)snprintf(line+13+i*2,3,"%02x",data[i]);line[13+n*2]='\n';if(!writeHost(line,14+n*2)){f.close();return;}}
 f.close();int n=snprintf(line,sizeof(line),"SM_DIAG_END %u %08x %u 0\n",total,crc^0xFFFFFFFFu,storageErrors);writeHost(line,size_t(n));
}
}
void setup(){
 setCpuFrequencyMhz(80);bootId=esp_random();
 pinMode(Boot,INPUT_PULLUP);pinMode(Pwr,INPUT);pinMode(Touch,INPUT);
 pinMode(46,OUTPUT);digitalWrite(46,LOW); // Amplifier stays off; codec/I2S never started.
 Wire.begin(15,14);Wire.setTimeOut(100);
 pmuOk=pmu.begin(Wire,AXP2101_SLAVE_ADDRESS,15,14);
 if(pmuOk){pmu.enableBattDetection();pmu.enableBattVoltageMeasure();pmu.enableVbusVoltageMeasure();}
 pinMode(9,OUTPUT);digitalWrite(9,LOW);delay(10);digitalWrite(9,HIGH);delay(100);
 screenOk=panel.begin();serialPower();storageOk=SPIFFS.begin(false);
 record("boot");
 WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(false);
 wifi_config_t saved{};if(esp_wifi_get_config(WIFI_IF_STA,&saved)==ESP_OK){memcpy(ssid,saved.sta.ssid,32);memcpy(password,saved.sta.password,64);haveWifi=ssid[0]!=0;}wifiOff();
 screen("Probe only: no mining","Unplug, press BOOT","10s, 60s, then 3min","Plug in to export");
}
void loop(){
 serialPower();
 static uint32_t checked=0;static int low=0;
 if(uint32_t(millis()-checked)>=5000){
  checked=millis();int pct=percent(),mv=voltage();
  if(powerKnown()&&!usb()&&pct>=0&&pct<=2&&mv>0&&mv<3400)++low;else low=0;
  if(low>=2){record("low_battery_shutdown");wifiOff();dark();pmu.shutdown();}
 }
 static char command[40];static size_t used=0;static bool overflow=false;
 while(serialOn&&Serial.available()){
  char c=Serial.read();if(c=='\n'){command[used]=0;if(!overflow&&!strcmp(command,"SM_DIAG_EXPORT"))exportLog();used=0;overflow=false;}else if(c!='\r'){if(used+1<sizeof(command))command[used++]=c;else overflow=true;}
 }
 bool pressed=digitalRead(Boot)==LOW;
 if(!pressed)inputReady=true;
 if(pressed&&inputReady){inputReady=false;run();}
 // Idle screen also sleeps after 15 seconds; touching/PWR wakes the instructions.
 static uint32_t last=millis();if(input()){last=millis();if(screenAsleep)screen("Probe idle","BOOT: run test","Plug in to export");}
 if(uint32_t(millis()-last)>15000)dark();
 delay(50);
}
