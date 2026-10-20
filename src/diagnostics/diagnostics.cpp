#include "diagnostics.h"
#include "record.h"
#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <atomic>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_ota_ops.h>
#include <time.h>
#include "version.h"
#include "network/pool_status.h"
#include "network/wifi_connection.h"
#include "device/mining_control.h"
#include "device/device_power.h"
#include "device/sleep_control.h"
#include "drivers/audio/audio.h"
#include "seriallog.h"

// Fail this translation unit if a diagnostic record format disagrees with its arguments.
#pragma GCC diagnostic error "-Wformat"

namespace diagnostics {
static std::atomic<int> mv{-1},raw{-1},pct{-1},source{-1},charge{-1},usb{-1};
static std::atomic<uint32_t> khRate{0},rateStamp{0},accepted{0},rejected{0},retries{0},dropped{0};
struct Entry { Event type; uint64_t ms; };
static QueueHandle_t queue=nullptr;
static TaskHandle_t task=nullptr;
static std::atomic<bool> urgent{false};
static char buffer[BufferBytes]; static size_t used=0;
static uint32_t boot=0, storageErrors=0;
static char bootLine[384];
static std::atomic<uint32_t> batteryStamp{0};
static bool storage=false;
static const char* current="/diagnostics-current.jsonl";
static const char* previous="/diagnostics-previous.jsonl";
static uint64_t nowMs(){return uint64_t(esp_timer_get_time())/1000;}
void batteryRaw(int v,int r,int s,int c){mv=v;raw=r;source=s;charge=c;batteryStamp=millis();}
void batteryDisplayed(int p){pct=p;}
void usbState(bool u){usb=u?1:0;}
void miningRate(uint32_t kh,uint32_t elapsedMs){
  khRate=elapsedMs?uint32_t(uint64_t(kh)*1000/elapsedMs):0; rateStamp=millis();
}
void event(Event e){
  if(e==Event::Accepted){++accepted;return;}
  if(e==Event::Rejected){++rejected;return;}
  if(e==Event::WifiRetry)++retries;
  Entry row{e,nowMs()};
  if(queue && xQueueSend(queue,&row,0)!=pdTRUE)++dropped;
}
void prepareForLightSleep(){if(task)xTaskNotifyGive(task);}
void checkpoint(){
  if(!task)return;
  event(Event::Shutdown);urgent=true;xTaskNotifyGive(task);
  const uint32_t start=millis();while(urgent && millis()-start<400) vTaskDelay(pdMS_TO_TICKS(10));
}
static void flush(){
  if(!used || !storage)return;
  File f=SPIFFS.open(current,FILE_READ);size_t size=f?f.size():0;f.close();
  if(size+used>SegmentBytes){
    if(SPIFFS.exists(previous) && !SPIFFS.remove(previous)){++storageErrors;return;}
    if(SPIFFS.exists(current) && !SPIFFS.rename(current,previous)){++storageErrors;return;}
  }
  // Preserve headroom for configuration writes and SPIFFS garbage collection.
  if(!canAppend(SPIFFS.totalBytes(),SPIFFS.usedBytes(),used+sizeof(bootLine))){++storageErrors;used=0;return;}
  f=SPIFFS.open(current,FILE_APPEND);
  if(!f){++storageErrors;return;}
  if(size+used>SegmentBytes && bootLine[0]) { f.println(bootLine); }
  size_t n=f.write(reinterpret_cast<const uint8_t*>(buffer),used);f.flush();f.close();
  if(n!=used)++storageErrors;
  // Never replay a partial write. A separator isolates an interrupted JSON row.
  used=0;
}
static void append(const char* line){
  size_t n=strlen(line);if(n+1>=BufferBytes){++dropped;return;}
  if(used+n+1>BufferBytes)flush();
  if(used+n+1>BufferBytes){++dropped;return;}
  memcpy(buffer+used,line,n);used+=n;buffer[used++]='\n';
}
static const char* eventName(Event e){
  switch(e){
    case Event::DemoStart:return "demo_start";case Event::DemoCancel:return "demo_cancel";
    case Event::DemoEnd:return "demo_end";case Event::AudioReady:return "audio_ready";
    case Event::AudioStart:return "audio_start";case Event::AudioStop:return "audio_stop";
    case Event::AudioInitFailed:return "audio_init_failed";
    case Event::AudioDecodeFailed:return "audio_decode_failed";
    case Event::AudioOutputFailed:return "audio_output_failed";
    case Event::AudioTaskFailed:return "audio_task_failed";
    case Event::WifiRetry:return "wifi_retry";case Event::RealBlock:return "real_block";
    case Event::WifiDisconnected:return "wifi_disconnected";
    case Event::HashError:return "hash_hardware_error";case Event::SubmitFailed:return "submission_write_failed";
    case Event::PoolParseError:return "pool_parse_error";case Event::SettingsSaveFailed:return "settings_save_failed";
    case Event::SaverPause:return "saver_pause";case Event::SaverResume:return "saver_resume";
    case Event::SaverCheck:return "saver_wifi_check";case Event::SaverCheckOk:return "saver_wifi_ok";
    case Event::SaverCheckFailed:return "saver_wifi_failed";case Event::SaverRadioError:return "saver_radio_error";
    case Event::Shutdown:return "shutdown_requested";default:return "unknown";
  }
}
static bool send(const char* data,size_t size){
  uint32_t last=millis();
  while(size){
    if(!Serial || millis()-last>2000)return false;
    size_t room=Serial.availableForWrite();
    if(!room){vTaskDelay(pdMS_TO_TICKS(5));continue;}
    size_t n=Serial.write(reinterpret_cast<const uint8_t*>(data),min(room,size));
    data+=n;size-=n;if(n)last=millis();
  } return true;
}
static void exportLog(){
  flush();
  if(!send("\nSM_DIAG_BEGIN 1\n",17))return;
  uint32_t crc=0xFFFFFFFFu;size_t total=0;
  const char* paths[]={previous,current};
  for(const char* path:paths){
    File f=SPIFFS.open(path,FILE_READ);if(!f)continue;
    uint8_t bytes[128];char line[274];
    while(f.available()){
      size_t n=f.read(bytes,sizeof(bytes));if(!n)break;
      memcpy(line,"SM_DIAG_DATA ",13);
      static const char hex[]="0123456789abcdef";
      for(size_t i=0;i<n;++i){line[13+i*2]=hex[bytes[i]>>4];line[14+i*2]=hex[bytes[i]&15];}
      line[13+n*2]='\n';
      if(!send(line,14+n*2)){f.close();return;}
      crc=crcUpdate(crc,bytes,n);total+=n;
    } f.close();
  }
  char end[100];snprintf(end,sizeof(end),"SM_DIAG_END %u %08lx %u %u\n",unsigned(total),static_cast<unsigned long>(crc^0xFFFFFFFFu),storageErrors,dropped.load());
  send(end,strlen(end));
}
static void worker(void*){
  boot=esp_random();storage=SPIFFS.totalBytes()>0;if(!storage)++storageErrors;
  char line[1152];
  const char* brand="SOLOMINER";
  char elf[65];const auto* app=esp_ota_get_app_description();
  for(int i=0;i<32;++i)snprintf(elf+i*2,3,"%02x",app->app_elf_sha256[i]);
  append(""); // isolate a torn row left by a previous power loss
  snprintf(bootLine,sizeof(bootLine),"{\"t\":\"boot\",\"b\":%u,\"ms\":%llu,\"schema\":1,\"fw\":\"%s\",\"build\":\"%s %s\",\"brand\":\"%s\",\"reset\":%d,\"elf\":\"%s\"}",boot,nowMs(),CURRENT_VERSION,__DATE__,__TIME__,brand,int(esp_reset_reason()),elf);append(bootLine);flush();
  uint32_t sampled=0,flushed=millis();uint64_t prior=nowMs(),onMs=0,linkedMs=0,miningMs=0,khSum=0;
  WatchState state,old;int oldWifi=-2,oldPool=-2,oldThermal=-2,oldDuty=-2,oldCpu=-2,oldSaver=-2,oldIdle=-2,oldSaverPhase=-2,oldWifiReason=-2;
  bool first=true,serialOn=!g_serialDisabled.load();char command[40];size_t commandSize=0;bool commandOverflow=false;
  for(;;){
    device::lightSleepCheckpoint(device::SleepRole::Diagnostics);
    uint64_t now=nowMs(),dt=now-prior;prior=now;
    const int power=usb.load();
    if(power==1 && !serialOn){Serial.begin(115200);Serial.setTimeout(0);serialOn=true;g_serialDisabled=false;}
    else if(power==0 && serialOn){g_serialDisabled=true;Serial.end();serialOn=false;commandSize=0;commandOverflow=false;}
    // Account elapsed time using the previously observed state, never new USB state retroactively.
    if(!first){if(old.screen>0)onMs+=dt;if(oldWifi==1)linkedMs+=dt;if(oldWifi==1&&oldPool==POOL_PHASE_MINING&&oldThermal<3&&oldIdle!=1)miningMs+=dt;}
    watchDiagnosticState(state);state.usb=power;
    int wifi=WiFi.status()==WL_CONNECTED?1:0,pool=g_poolPhase.load(),thermal=g_thermalTier.load();
    int duty=max(g_miningDutyCycleMs.load(),g_thermalDutyFloorMs.load()),cpu=getCpuFrequencyMhz();
    int saver=device::batterySaverEnabled()?1:0;
    int idle=device::batterySaverIdle()?1:0,saverPhase=network::saverPhase();
    int wifiReason=network::lastDisconnectReason();
    bool changed=first||wifiReason!=oldWifiReason||idle!=oldIdle||saverPhase!=oldSaverPhase||saver!=oldSaver||state.screen!=old.screen||state.page!=old.page||state.brightness!=old.brightness||state.demo!=old.demo||power!=old.usb||wifi!=oldWifi||pool!=oldPool||thermal!=oldThermal||duty!=oldDuty||cpu!=oldCpu;
    uint32_t rate=!idle&&millis()-rateStamp.load()<5000?khRate.load():0;
    if(!first)khSum+=uint64_t(rate)*dt/1000;
    if(changed || elapsed(millis(),sampled,60000)){
      const bool periodic=elapsed(millis(),sampled,60000)||first;
      const auto sleep=device::lightSleepStats();
      int temp=periodic?int(temperatureRead()*10):-999,rssi=wifi?WiFi.RSSI():-127;
      snprintf(line,sizeof(line),"{\"t\":\"%s\",\"b\":%u,\"ms\":%llu,\"utc\":%lld,\"battery_age_ms\":%u,\"mv\":%d,\"raw\":%d,\"pct\":%d,\"src\":%d,\"chg\":%d,\"usb\":%d,\"screen\":%d,\"page\":%d,\"bright\":%d,\"demo\":%d,\"audio\":%d,\"hw\":%d,\"wifi\":%d,\"wifi_reason\":%d,\"rssi\":%d,\"pool\":%d,\"retry\":%u,\"mhz\":%d,\"saver\":%d,\"idle\":%d,\"saver_phase\":%d,\"duty\":%d,\"thermal\":%d,\"c10\":%d,\"kh\":%u,\"on_ms\":%llu,\"linked_ms\":%llu,\"mining_ms\":%llu,\"kh_sum\":%llu,\"accepted\":%u,\"rejected\":%u,\"storage_errors\":%u,\"min_heap\":%u,\"heap\":%u,\"drop\":%u,\"sleep_count\":%u,\"sleep_ms\":%u,\"sleep_failures\":%u,\"sleep_wake\":%d,\"sleep_error\":%d}",periodic?"s":"state",boot,now,static_cast<long long>(time(nullptr)),uint32_t(millis()-batteryStamp.load()),mv.load(),raw.load(),pct.load(),source.load(),charge.load(),power,state.screen,state.page,state.brightness,state.demo,audioIsPlaying(),audioHardwareReady(),wifi,wifiReason,rssi,pool,retries.load(),cpu,saver,idle,saverPhase,duty,thermal,temp,rate,onMs,linkedMs,miningMs,khSum,accepted.load(),rejected.load(),storageErrors,ESP.getMinFreeHeap(),ESP.getFreeHeap(),dropped.load(),sleep.count,sleep.milliseconds,sleep.failures,sleep.wake,sleep.error);append(line);
      if(periodic)sampled=millis();
    }
    first=false;old=state;oldWifi=wifi;oldPool=pool;oldThermal=thermal;oldDuty=duty;oldCpu=cpu;oldSaver=saver;oldIdle=idle;oldSaverPhase=saverPhase;oldWifiReason=wifiReason;
    Entry e;while(xQueueReceive(queue,&e,0)==pdTRUE){snprintf(line,sizeof(line),"{\"t\":\"event\",\"b\":%u,\"ms\":%llu,\"event\":\"%s\"}",boot,e.ms,eventName(e.type));append(line);}
    if(urgent.load() || (changed && power==1) || elapsed(millis(),flushed,300000)){flush();flushed=millis();urgent=false;}
    if(serialOn){
      int budget=64;while(budget-- && Serial.available()){
        char c=Serial.read();
        if(c=='\n'){
          command[commandSize]=0;
          if(!commandOverflow && !strcmp(command,"SM_DIAG_EXPORT")){exportLog();prior=nowMs();}
          commandSize=0;commandOverflow=false;
        }else if(c!='\r'){
          if(commandSize+1<sizeof(command))command[commandSize++]=c;else commandOverflow=true;
        }
      }
    }
    ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1000));
  }
}
void begin(){
  if(queue)return;
  queue=xQueueCreate(32,sizeof(Entry));if(!queue)return;
  if(xTaskCreate(worker,"Diagnostics",6144,nullptr,1,&task)!=pdPASS){vQueueDelete(queue);queue=nullptr;task=nullptr;}
}
}
