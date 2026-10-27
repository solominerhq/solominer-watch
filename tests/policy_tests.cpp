#include "diagnostics/record.h"
#include "ui/setup_code.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "device/runtime_policy.h"
#include "network/connection_policy.h"
#include "network/idle_pause.h"
#include "network/portal_cleanup.h"
#include "network/authorization.h"
#include "settings/config_codec.h"
#include "ui/mining_status.h"
#include "ui/page_controls.h"
#include "ui/celebration_policy.h"

// Model the dependency's unsafe stop: no HTTP server exists on saved-WiFi
// startup, or after the blocking setup portal has already closed itself.
struct PortalLifecycle {
  bool config, web, server;
  int stops = 0;
  PortalLifecycle(bool c, bool w) : config(c), web(w), server(c || w) {}
  bool getConfigPortalActive() const { return config; }
  bool getWebPortalActive() const { return web; }
  void stopConfigPortal() {
    assert(server); // WiFiManager dereferences this without a null check.
    config = false; server = false; ++stops;
  }
  void stopWebPortal() {
    assert(server);
    web = false; config = false; server = false; ++stops;
  }
};

int main() {
  for (int mode = 0; mode < 3; ++mode) {
    PortalLifecycle portal(mode == 1, mode == 2);
    network::stopProvisioningPortals(portal);
    assert(!portal.config && !portal.web && !portal.server);
    assert(portal.stops == (mode == 0 ? 0 : 1));
    network::stopProvisioningPortals(portal); // already closed: never stop twice
    assert(portal.stops == (mode == 0 ? 0 : 1));
  }

  policy::UsbDebounce usb;
  usb.reset(false,0);
  assert(!usb.update(true,100));
  assert(!usb.update(false,110)); // contact bounce never changes profile
  assert(!usb.update(true,200));
  assert(!usb.update(true,2199));
  assert(usb.update(true,2200) && usb.applied());
  usb.reset(false,0xffffff00u);
  assert(!usb.update(true,0xffffff10u));
  assert(usb.update(true,uint32_t(0xffffff10u + 2000u))); // timer wrap
  policy::CriticalBattery battery;
  assert(!battery.update(2,true));
  assert(!battery.update(-1,true)); // unavailable reading breaks streak
  assert(!battery.update(1,true));
  assert(!battery.update(1,false)); // connected or uncertain USB forbids shutdown
  assert(!battery.update(1,true));
  assert(battery.update(1,true));

  network::RetrySchedule retry;
  uint32_t now=0;
  const uint32_t gaps[]={3000,6000,12000,24000,30000,30000};
  for(auto gap:gaps) {
    assert(retry.due(now)); retry.tried(now);
    assert(!retry.due(now+gap-1)); now+=gap; assert(retry.due(now));
  }
  retry.reset(); assert(retry.due(5));
  retry.tried(0xffffff00u); assert(retry.due(uint32_t(0xffffff00u+3000u)));

  // Long hotspot outages slow Wi-Fi only. An explicit waiting-state retry
  // bypasses any delay. The runtime owner guards association/DHCP separately.
  network::WifiRetrySchedule wifiRetry;
  assert(wifiRetry.due(0)); wifiRetry.tried(0);
  assert(!wifiRetry.due(2999)); assert(wifiRetry.due(1,true));
  assert(wifiRetry.due(3000));
  for (uint32_t t=3000; t<300000; t+=1000)
    if (wifiRetry.due(t)) wifiRetry.tried(t);
  assert(!wifiRetry.due(300000));
  assert(wifiRetry.due(300000,true)); wifiRetry.tried(300000);
  assert(!wifiRetry.due(300001)); assert(wifiRetry.due(300001,true));
  assert(!wifiRetry.due(359999)); assert(wifiRetry.due(360000));
  wifiRetry.reset(); assert(wifiRetry.due(360001)); wifiRetry.tried(360001);
  assert(!wifiRetry.due(363000)); assert(wifiRetry.due(363001));
  wifiRetry.reset(); const uint32_t start=0xfffffff0u; wifiRetry.tried(start);
  assert(!wifiRetry.due(uint32_t(start+2999)));
  assert(wifiRetry.due(uint32_t(start+1),true));
  assert(wifiRetry.due(uint32_t(start+3000)));
  wifiRetry.tried(uint32_t(start+300000));
  assert(!wifiRetry.due(uint32_t(start+359999)));
  assert(wifiRetry.due(uint32_t(start+360000)));

  network::PoolSession session;
  session.begin(2,0);
  session.receivedJob(1); assert(!session.ready()); // pre-auth work never enables mining
  assert(!session.authExpired(14999)); assert(session.authExpired(15000));
  session.reply(true); session.receivedJob(15001); assert(session.ready());
  assert(!session.stale(15001+60000)); // zero hashes / thermal pause do not matter
  assert(!session.stale(15001+179999)); assert(session.stale(15001+180000));
  session.begin(3,20000); assert(!session.ready()); // old job invalid after reconnect
  session.reply(false); assert(!session.ready());
  using network::AuthReply;
  assert(network::authorizationReply(R"({"id":2,"result":true,"error":null})",2)==AuthReply::Accepted);
  assert(network::authorizationReply(R"({"id":2,"result":false,"error":null})",2)==AuthReply::Rejected);
  assert(network::authorizationReply(R"({"id":2,"result":true,"error":[24,"bad login"]})",2)==AuthReply::Rejected);
  assert(network::authorizationReply(R"({"id":2,"result":"true"})",2)==AuthReply::Rejected);
  assert(network::authorizationReply(R"({"id":3,"result":true})",2)==AuthReply::Unrelated);
  assert(network::authorizationReply(R"({"method":"mining.notify","id":null})",2)==AuthReply::Unrelated);
  assert(network::authorizationReply("not json",2)==AuthReply::Unrelated);

  StaticJsonDocument<1024> json;
  TSettings saved;
  deserializeJson(json,R"({"ultraSaver":true,"btcString":"  example-wallet  ","workerName":" Wrist ","gmtZone":-5,"portNumber":1234,"clockFormat":24,"screenBrightness":230,"poolPassword":"secret"})");
  settings::readConfig(json.as<JsonObjectConst>(),saved,settings::ConfigFormat::Internal);
  assert(!strcmp(saved.BtcWallet,"example-wallet")); assert(!strcmp(saved.WorkerName,"Wrist"));
  assert(!strcmp(saved.PosixTZ,"UTC5")); assert(!strcmp(saved.PoolPassword,"secret"));
  assert(saved.PoolPort==1234 && saved.clockFormat==24 && saved.screenBrightness==230);
  assert(saved.batterySaver); // Missing key defaults on.
  saved.batterySaver=false;
  json.clear(); settings::writeConfig(json.to<JsonObject>(),saved);
  assert(!json.containsKey("ultraSaver"));
  TSettings roundtrip;
  settings::readConfig(json.as<JsonObjectConst>(),roundtrip,settings::ConfigFormat::Internal);
  assert(!roundtrip.batterySaver); // Saved Off survives save/reload.
  assert(!strcmp(roundtrip.BtcWallet,saved.BtcWallet)); assert(!strcmp(roundtrip.PosixTZ,saved.PosixTZ));
  deserializeJson(json,R"({"SSID":"iPhone","WifiPW":"wifi-pass","UltraSaver":true,"WorkerName":"Watch2","PoolPort":-1,"ClockFormat":99,"ScreenBrightness":999,"PosixTZ":"UTC0"})");
  settings::readConfig(json.as<JsonObjectConst>(),roundtrip,settings::ConfigFormat::SdCard);
  assert(!strcmp(roundtrip.WifiSSID.c_str(),"iPhone")); assert(!strcmp(roundtrip.WifiPW.c_str(),"wifi-pass"));
  assert(!strcmp(roundtrip.WorkerName,"Watch2")); assert(!strcmp(roundtrip.PosixTZ,"UTC0"));
  assert(roundtrip.PoolPort==DEFAULT_POOLPORT && roundtrip.clockFormat==12 && roundtrip.screenBrightness==150);
  deserializeJson(json,"{}"); TSettings defaults;
  settings::readConfig(json.as<JsonObjectConst>(),defaults,settings::ConfigFormat::Internal);
  assert(!strcmp(defaults.PosixTZ,""));
  assert(defaults.batterySaver);
  deserializeJson(json,R"({"idleBatterySaver":"true"})");
  settings::readConfig(json.as<JsonObjectConst>(),defaults,settings::ConfigFormat::Internal);
  assert(defaults.batterySaver); // Reject malformed setting.
  deserializeJson(json,R"({"IdleBatterySaver":false})");
  settings::readConfig(json.as<JsonObjectConst>(),defaults,settings::ConfigFormat::SdCard);
  assert(!defaults.batterySaver);
  assert(!ui::showHashrate(false,POOL_PHASE_MINING,false));
  assert(!ui::showHashrate(true,POOL_PHASE_AUTHORIZING,false));
  assert(!ui::showHashrate(true,POOL_PHASE_MINING,true));
  assert(ui::showHashrate(true,POOL_PHASE_MINING,false));
  assert(!strcmp(ui::miningStatus(true,POOL_PHASE_AUTH_REJECTED,false),"Check pool settings"));
  assert(!strcmp(ui::miningStatus(false,POOL_PHASE_MINING,false),"Connecting to Wi-Fi"));
  assert(!strcmp(ui::miningStatus(true,POOL_PHASE_MINING,true),"Cooling down"));
  // Losing Wi-Fi takes precedence over a stale pool status and offers retry.
  for (int phase = 0; phase <= POOL_PHASE_MINING; ++phase)
    assert(ui::connectionStatus(false,phase,false).action == ui::StatusAction::RetryWifi);
  assert(ui::connectionStatus(true,POOL_PHASE_AUTH_REJECTED,false).action == ui::StatusAction::OpenSetup);
  assert(ui::connectionStatus(true,POOL_PHASE_WAITING_JOB,false).tone == ui::StatusTone::Progress);
  assert(ui::connectionStatus(true,POOL_PHASE_MINING,true).action == ui::StatusAction::None);
  assert(ui::connectionStatus(true,POOL_PHASE_MINING,false,false).tone == ui::StatusTone::Progress);
  char qr[256];
  assert(ui::setupQrPayload(qr,sizeof(qr),"My;Watch", "a:b"));
  assert(!strcmp(qr,"WIFI:T:WPA;S:My\\;Watch;P:a\\:b;;"));
  assert(ui::setupQrPayload(qr,sizeof(qr),"Richard's iPhone", "password"));
  const size_t length = strlen(qr);
  assert(ui::setupQrPayload(qr,length+1,"Richard's iPhone", "password"));
  assert(!ui::setupQrPayload(qr,length,"Richard's iPhone", "password") && !qr[0]);
  assert(!ui::setupQrPayload(nullptr,10,"Watch","password"));
  assert(diagnostics::canAppend(1024*1024,512*1024,4096));
  assert(!diagnostics::canAppend(1024*1024,900*1024,4096));
  assert(!diagnostics::canAppend(1024,2048,1));
  const uint8_t crcExample[]="123456789";
  assert((diagnostics::crcUpdate(0xFFFFFFFFu,crcExample,9)^0xFFFFFFFFu)==0xCBF43926u);
  assert(diagnostics::elapsed(20,0xFFFFFFF0u,30));
  deserializeJson(json,R"({"batterySaver":true,"BatterySaver":true})");
  settings::readConfig(json.as<JsonObjectConst>(),defaults,settings::ConfigFormat::Internal);
  assert(!defaults.batterySaver); // Legacy keys do not override the saved v4.3 choice.
  assert(!policy::saverIdle(true,false,60000,0));
  assert(!policy::saverIdle(true,false,179999,0));
  assert(policy::saverIdle(true,false,180000,0));
  assert(!policy::saverIdle(false,false,180000,0));
  assert(!policy::saverIdle(true,true,180000,0));
  assert(!policy::saverIdle(true,false,180000,179999));
  assert(!policy::saverIdle(true,false,uint32_t(0xfffffff0u+179999),0xfffffff0u));
  assert(policy::saverIdle(true,false,uint32_t(0xfffffff0u+180000),0xfffffff0u));
  using network::IdleAction;
  network::IdlePause pause;
  assert(pause.step(false)==IdleAction::None);
  assert(pause.step(true)==IdleAction::Stop);
  for(int i=0;i<10000;++i)assert(pause.step(true)==IdleAction::None);
  assert(pause.step(false)==IdleAction::Resume);
  assert(pause.step(false)==IdleAction::None);
  assert(pause.step(true)==IdleAction::Stop);
  auto address=ui::connectionStatus(false,POOL_PHASE_IDLE,false,true,0,false,network::WifiStage::GettingAddress);
  assert(!strcmp(address.clock,"Connecting to Wi-Fi") && address.action==ui::StatusAction::None);
  auto joining=ui::connectionStatus(false,POOL_PHASE_IDLE,false,true,0,false,network::WifiStage::Connecting);
  assert(joining.tone==ui::StatusTone::Wifi && joining.action==ui::StatusAction::None);
  auto paused=ui::connectionStatus(false,POOL_PHASE_IDLE,false,true,0,true);
  assert(paused.action==ui::StatusAction::None && !strcmp(paused.clock,"Battery Saver"));
  assert(paused.tone==ui::StatusTone::Neutral && !paused.detail[0]);
  // Wi-Fi stays orange through every retry stage; guidance appears on sustained
  // outages. Saver and a recovered link suppress old warnings.
  for (auto stage : {network::WifiStage::Waiting,network::WifiStage::Connecting,network::WifiStage::GettingAddress}) {
    auto normal=ui::connectionStatus(false,POOL_PHASE_AUTH_REJECTED,false,false,4,false,stage,false);
    auto trouble=ui::connectionStatus(false,POOL_PHASE_AUTH_REJECTED,false,false,4,false,stage,true);
    assert(normal.tone==ui::StatusTone::Wifi && !normal.detail[0]);
    assert(!strcmp(normal.clock,"Connecting to Wi-Fi"));
    assert(trouble.tone==ui::StatusTone::Wifi && !strcmp(trouble.detail,"Check network or hotspot"));
    assert(!strcmp(trouble.clock,normal.clock));
    auto saver=ui::connectionStatus(false,POOL_PHASE_AUTH_REJECTED,true,false,4,true,stage,true);
    assert(saver.tone==ui::StatusTone::Neutral && !strcmp(saver.clock,"Battery Saver"));
  }
  for (int phase : {POOL_PHASE_IDLE,POOL_PHASE_CONNECTING,POOL_PHASE_RETRYING,POOL_PHASE_SUBSCRIBING,POOL_PHASE_AUTHORIZING}) {
    auto normal=ui::connectionStatus(true,phase,false,false,0);
    auto trouble=ui::connectionStatus(true,phase,false,false,4);
    assert(!strcmp(normal.clock,"Joining pool") && normal.tone==ui::StatusTone::Progress);
    assert(!strcmp(trouble.clock,"Pool unavailable") && trouble.tone==ui::StatusTone::Progress);
  }
  assert(ui::connectionStatus(true,POOL_PHASE_AUTH_REJECTED,false).tone==ui::StatusTone::Progress);
  assert(!strcmp(ui::connectionStatus(true,POOL_PHASE_WAITING_JOB,false,false).clock,"Waiting for work"));
  assert(!strcmp(ui::connectionStatus(true,POOL_PHASE_MINING,false,false).clock,"Starting mining"));
  assert(!strcmp(ui::connectionStatus(true,POOL_PHASE_MINING,true,true).detail,"Resumes automatically"));
  auto mining=ui::connectionStatus(true,POOL_PHASE_MINING,false,true,0,false,network::WifiStage::Connected,true);
  assert(!strcmp(mining.clock,"Mining") && mining.tone==ui::StatusTone::Good && !mining.detail[0]);
  network::WifiRetrySchedule warning;
  assert(!warning.needsAttention(60000)); // No active attempt; never warn.
  warning.tried(0);
  assert(!warning.needsAttention(29999));
  warning.tried(3000);warning.tried(9000); // Retries do not restart the deadline.
  assert(warning.needsAttention(30000) && warning.needsAttention(60000));
  warning.reset();assert(!warning.needsAttention(60000));
  warning.tried(0xfffffff0u);
  assert(!warning.needsAttention(uint32_t(0xfffffff0u+29999)));
  assert(warning.needsAttention(uint32_t(0xfffffff0u+30000)));
  assert(!strcmp(ui::connectionStatus(false,POOL_PHASE_IDLE,false,true,0,false,network::WifiStage::Connecting).clock,"Connecting to Wi-Fi"));
  assert(!ui::advanceCelebration(3999,0,false,false,true));
  assert(ui::advanceCelebration(4000,0,false,false,true));
  assert(!ui::advanceCelebration(11999,0,true,false,true));
  assert(ui::advanceCelebration(12000,0,true,false,true));
  assert(!ui::advanceCelebration(60000,0,true,true,true));
  assert(!ui::advanceCelebration(60000,0,false,true,true));
  assert(ui::advanceCelebration(4000,0,false,false,false));
  assert(!ui::advanceCelebration(60000,0,true,false,false));
  assert(ui::advanceCelebration(uint32_t(0xfffffff0u+12000),0xfffffff0u,true,false,true));
  using ui::PageAction;
  assert(ui::clockStatus.contains(20,369) && ui::clockStatus.contains(389,445));
  assert(!ui::clockStatus.contains(205,348) && !ui::clockStatus.contains(205,453));
  assert(!ui::clockStatus.contains(205,485)); // Clock retry target excludes page dots.
  using ui::SettingsView;
  const auto list = SettingsView::List;
  assert(ui::settingsAction(30,120,list)==PageAction::Brightness);
  assert(ui::settingsAction(30,250,list)==PageAction::BatterySaver);
  assert(ui::settingsAction(30,380,list)==PageAction::ShowSetup);
  // Each full row, including its corners and right-side value/icon, is tappable.
  const ui::PageRect rows[] = {ui::brightness,ui::batterySaver,ui::setup};
  const PageAction actions[] = {PageAction::Brightness,PageAction::BatterySaver,PageAction::ShowSetup};
  for (int i=0;i<3;++i) {
    const auto& r=rows[i];
    assert(r.height>=100 && r.x>=20 && r.x+r.width<=390);
    for (int x : {r.x,r.x+r.width/2,r.x+r.width-1})
      for (int y : {r.y,r.y+r.height/2,r.y+r.height-1})
        assert(ui::settingsAction(x,y,list)==actions[i]);
  }
  // Gaps, outside edges and the navigation dots never trigger a row.
  for(int y : {103,204,219,320,335,436,450,471})
    assert(ui::settingsAction(205,y,list)==PageAction::None);
  assert(ui::settingsAction(23,120,list)==PageAction::None);
  assert(ui::settingsAction(386,120,list)==PageAction::None);
  // The former demo row now belongs to Wi-Fi & mining; its bottom tail is blank.
  assert(ui::settingsAction(385,400,list)==PageAction::ShowSetup);
  assert(ui::settingsAction(385,439,list)==PageAction::None);
  for(auto detail : {SettingsView::BatterySaver,SettingsView::Brightness,SettingsView::SetupConfirmation})
    assert(ui::settingsAction(50,50,detail)==PageAction::Back);
  assert(ui::settingsAction(205,440,SettingsView::BatterySaver)==PageAction::None);
  for(int x : {32,205,377}) for(int y : {117,160,204})
    assert(ui::settingsAction(x,y,SettingsView::BatterySaver)==PageAction::ToggleSaver);
  for(int y : {116,205,240,400})
    assert(ui::settingsAction(50,y,SettingsView::BatterySaver)==PageAction::None);
  const ui::PageRect choices[] = {ui::brightnessLow,ui::brightnessMedium,ui::brightnessHigh};
  const PageAction picks[] = {PageAction::BrightnessLow,PageAction::BrightnessMedium,PageAction::BrightnessHigh};
  const int levels[] = {60,150,230};
  for(int i=0;i<3;++i) {
    for(int x : {32,205,377}) for(int y : {choices[i].y,choices[i].y+40,choices[i].y+87})
      assert(ui::settingsAction(x,y,SettingsView::Brightness)==picks[i]);
    assert(ui::selectedBrightness(picks[i])==levels[i]);
  }
  assert(ui::selectedBrightness(PageAction::Back)==-1);
  for(int y : {116,205,220,309,324,413,450})
    assert(ui::settingsAction(205,y,SettingsView::Brightness)==PageAction::None);
  const auto confirm = SettingsView::SetupConfirmation;
  for(int x : {32,205,377}) for(int y : {282,326,369})
    assert(ui::settingsAction(x,y,confirm)==PageAction::OpenSetup);
  for(int x : {32,205,377}) for(int y : {386,430,473})
    assert(ui::settingsAction(x,y,confirm)==PageAction::Back);
  for(int y : {130,220,281,370,385,474,485}) assert(ui::settingsAction(205,y,confirm)==PageAction::None);
  using ui::SetupView; using ui::SetupAction;
  assert(ui::setupAction(100,440,SetupView::Qr)==SetupAction::Manual);
  assert(ui::setupAction(300,440,SetupView::Qr)==SetupAction::Exit);
  assert(ui::setupAction(100,440,SetupView::Failed)==SetupAction::Retry);
  assert(ui::setupAction(300,440,SetupView::Failed)==SetupAction::Exit);
  assert(ui::setupAction(205,440,SetupView::Qr)==SetupAction::None); // gap
  assert(ui::setupAction(50,50,SetupView::Manual)==SetupAction::Qr);
  assert(ui::setupAction(205,440,SetupView::Manual)==SetupAction::Exit);
  for(auto view : {SetupView::Connecting,SetupView::Leaving})
    assert(ui::setupAction(300,440,view)==SetupAction::None);
  assert(ui::demoAction(50,454)==ui::DemoAction::ToggleMute);
  assert(ui::demoAction(355,454)==ui::DemoAction::Exit);
  for (int x : {170,180,198,205,213,230,245})
    assert(ui::demoAction(x,471)==ui::DemoAction::None);
  assert(ui::demoAction(50,45)==ui::DemoAction::None);
  assert(ui::demoAction(50,397)==ui::DemoAction::None);
  assert(ui::demoAction(50,482)==ui::DemoAction::None);
  puts("PASS: Settings hit targets and confirmation, status guidance; portal cleanup, power transitions, timer wrap, battery protection, retries, authorization, job freshness and settings migration");
}
