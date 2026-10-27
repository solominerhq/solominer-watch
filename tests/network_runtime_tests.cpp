#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "WiFi.h"
#include "network/wifi_connection.h"
#include "diagnostics/diagnostics.h"
FakeWiFi WiFi;
static uint32_t now=0;
static int connectionNotifications=0;
namespace device { bool noteWifiConnected(){++connectionNotifications;return true;} }
static bool failStart=false,failStop=false;
static int starts=0,stops=0,checks=0,pauses=0,resumes=0,disconnects=0;
uint32_t millis(){return now;}
void init_WifiManager(){}
namespace diagnostics {
void event(Event e){
 if(e==Event::SaverCheck)++checks;
 if(e==Event::SaverPause)++pauses;
 if(e==Event::SaverResume)++resumes;
 if(e==Event::WifiDisconnected)++disconnects;
}
}
int esp_wifi_start(){++starts;return failStart?-1:0;}
// Deliberately retain stale WL_CONNECTED to model asynchronous stop events.
int esp_wifi_stop(){++stops;return failStop?-1:0;}
static void connect(){
 WiFi.gotIp();assert(!network::service());now+=2000;assert(network::service());
 assert(WiFi.sleep==WIFI_PS_MIN_MODEM);
}
int main(){
 network::begin();assert(!network::radioAsleep());assert(!network::takeSaverPauseEvent());
 using Stage=network::WifiStage;
 assert(network::wifiStage()==Stage::Connecting);
 assert(!network::service());
 assert(!network::wifiNeedsAttention());
 now=29999;assert(!network::service());assert(!network::wifiNeedsAttention());
 now=30000;assert(!network::service());assert(network::wifiNeedsAttention());
 network::requestReconnect();now+=3000;assert(!network::service());
 assert(network::wifiNeedsAttention()); // Explicit retry does not reset the warning.
 WiFi.event(ARDUINO_EVENT_WIFI_STA_CONNECTED);assert(network::wifiStage()==Stage::GettingAddress);
 assert(network::wifiNeedsAttention()); // Association without an IP is not recovery.
 connect();
 assert(!network::wifiNeedsAttention());
 assert(network::successfulConnections()==1 && connectionNotifications==1);
 WiFi.gotIp();assert(network::successfulConnections()==1 && connectionNotifications==1); // Renewal is not a new connection.
 network::serviceIdle();assert(network::radioAsleep());assert(stops==1&&network::saverPhase()==1&&pauses==1);
 assert(network::takeSaverPauseEvent());assert(!network::takeSaverPauseEvent());
 const int requests=WiFi.reconnects;
 // An hour idle has no radio starts, reconnects, checks or repeated stop calls.
 for(int i=0;i<3600;++i){now+=1000;network::serviceIdle();}
 assert(starts==0&&WiFi.reconnects==requests&&checks==0&&stops==1);
 assert(!network::takeSaverPauseEvent()); // Idle servicing never retriggers navigation.
 // A queued reconnect request must not wake idle Wi-Fi.
 network::requestReconnect();now+=60000;network::serviceIdle();
 assert(starts==0&&WiFi.reconnects==requests);
 // Returning to foreground starts once and must obtain a fresh IP.
 assert(!network::service());assert(starts==1&&resumes==1&&network::saverPhase()==0);assert(!network::radioAsleep());
 now+=2100;assert(!network::service());connect();
 // Repeated sleep/wake, including millis rollover, never schedules an idle check.
 now=0xfffffff0u;
 for(int i=0;i<30;++i){
  network::serviceIdle();int before=starts;
  now+=3600000;network::serviceIdle();assert(starts==before);
  assert(!network::service());assert(starts==before+1);connect();
  assert(network::takeSaverPauseEvent()); // A fast wake cannot lose the reset.
  assert(!network::takeSaverPauseEvent());
 }
 assert(checks==0);
 // Radio start errors use bounded foreground retries, then recover.
 network::serviceIdle();failStart=true;
 assert(network::takeSaverPauseEvent());
 assert(!network::service());int before=starts;
 now+=100;assert(!network::service());assert(starts==before);
 now+=3000;assert(!network::service());assert(starts==before+1);
 // A user wake also bypasses a failed radio-start wait, and the successful
 // start must begin association on this same service call.
 failStart=false;now+=1;before=starts;int beforeReconnect=WiFi.reconnects;
 network::requestReconnect();assert(!network::service());
 assert(starts==before+1 && WiFi.reconnects==beforeReconnect+1);
 connect();
 // Radio stop errors are retried with backoff, never by reconnecting.
 failStop=true;before=stops;int beforeStarts=starts,beforeRequests=WiFi.reconnects;
 network::serviceIdle();assert(stops==before+1);assert(!network::radioAsleep());
 assert(!network::takeSaverPauseEvent()); // A failed stop is not a shutdown.
 now+=100;network::serviceIdle();assert(stops==before+1);
 now+=3000;network::serviceIdle();assert(stops==before+2);
 failStop=false;now+=6000;network::serviceIdle();assert(stops==before+3);
 assert(network::takeSaverPauseEvent());assert(!network::takeSaverPauseEvent());
 now+=3600000;network::serviceIdle();
 assert(starts==beforeStarts&&WiFi.reconnects==beforeRequests&&checks==0);
 assert(!network::service());connect();
 // A new outage gets a fresh 30-second window; hours of Saver sleep do not
 // count against that window or make the next wake immediately orange.
 WiFi.state=0;network::service();assert(!network::wifiNeedsAttention());
 now+=30000;network::service();assert(network::wifiNeedsAttention());
 network::serviceIdle();assert(!network::wifiNeedsAttention());
 assert(network::takeSaverPauseEvent()); // Consume this deliberate pause.
 now+=3600000;network::serviceIdle();assert(!network::wifiNeedsAttention());
 assert(!network::service());assert(!network::wifiNeedsAttention());
 connect();assert(!network::wifiNeedsAttention());
 // Local leave during reconnect is not a failed association.
 WiFi.state=0;network::requestReconnect();network::service();
 assert(network::wifiStage()==Stage::Connecting);
 int previousDisconnects=disconnects;
 WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,WIFI_REASON_ASSOC_LEAVE);
 assert(network::wifiStage()==Stage::Connecting&&disconnects==previousDisconnects);
 WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,201);
 assert(network::wifiStage()==Stage::Waiting&&network::lastDisconnectReason()==201);
 assert(disconnects==previousDisconnects+1);
 WiFi.acceptReconnect=false;now+=3000;network::service();assert(network::wifiStage()==Stage::Waiting);
 WiFi.acceptReconnect=true;now+=6000;network::service();assert(network::wifiStage()==Stage::Connecting);
 assert(network::wifiProblem()==network::WifiProblem::NotFound);
 connect();assert(network::lastDisconnectReason()==201);
 assert(network::wifiProblem()==network::WifiProblem::Unknown);
 WiFi.state=0;WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,202);
 assert(!network::service());
 assert(network::wifiProblem()==network::WifiProblem::Credentials);
 connect();assert(network::wifiProblem()==network::WifiProblem::Unknown);
 WiFi.event(ARDUINO_EVENT_WIFI_STA_LOST_IP);assert(network::wifiStage()==Stage::GettingAddress);
 assert(!network::service());
 WiFi.event(ARDUINO_EVENT_WIFI_STA_STOP);assert(network::wifiStage()==Stage::Waiting);
 assert(!network::takeSaverPauseEvent()); // Ordinary link loss is not Battery Saver.
 connect();
 // Wake immediately after a failed attempt during a long outage. This used
 // to lose the request inside a three-second guard and wait up to a minute.
 // Run once normally and once across millis rollover.
 for(uint32_t start : {10000000u,0xfffffff0u}) {
  now=start;
  WiFi.state=0;WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,201);
  assert(!network::service());
  now+=300000;assert(!network::service());
  WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,201);
  beforeReconnect=WiFi.reconnects;
  now+=1;assert(!network::service());assert(WiFi.reconnects==beforeReconnect);
  network::requestReconnect();network::requestReconnect(); // Coalesce repeated requests.
  assert(!network::service());assert(WiFi.reconnects==beforeReconnect+1);
  assert(network::wifiStage()==Stage::Connecting && network::wifiNeedsAttention());
  assert(!network::service());assert(WiFi.reconnects==beforeReconnect+1);
  // Waking cannot restart an in-progress association or DHCP negotiation,
  // even after the former three-second guard has elapsed.
  now+=4000;network::requestReconnect();assert(!network::service());
  assert(WiFi.reconnects==beforeReconnect+1);
  WiFi.event(ARDUINO_EVENT_WIFI_STA_CONNECTED);
  now+=4000;network::requestReconnect();assert(!network::service());
  assert(network::wifiStage()==Stage::GettingAddress && WiFi.reconnects==beforeReconnect+1);
  connect();network::requestReconnect();assert(network::service());
  assert(WiFi.reconnects==beforeReconnect+1 && !network::wifiNeedsAttention());
  // The request consumed while connected cannot retry a subsequent outage.
  WiFi.state=0;WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,201);
  assert(!network::service());beforeReconnect=WiFi.reconnects;
  WiFi.event(ARDUINO_EVENT_WIFI_STA_DISCONNECTED,201);
  now+=1;assert(!network::service());assert(WiFi.reconnects==beforeReconnect);
  connect();
 }
 puts("PASS: radio-off idle, immediate wake retries, association/DHCP protection, fresh IP, rollover, start/stop failures and disconnect diagnostics");
}
