#pragma once
using WiFiEvent_t=int;
struct WiFiEventInfo_t { struct { int reason=0; } wifi_sta_disconnected; };
constexpr int WL_CONNECTED=3,WIFI_PS_NONE=0,WIFI_PS_MIN_MODEM=1;
constexpr int ARDUINO_EVENT_WIFI_STA_GOT_IP=7,ARDUINO_EVENT_WIFI_STA_CONNECTED=4,ARDUINO_EVENT_WIFI_STA_DISCONNECTED=5,ARDUINO_EVENT_WIFI_STA_LOST_IP=8,ARDUINO_EVENT_WIFI_STA_STOP=3,WIFI_REASON_ASSOC_LEAVE=8;
struct FakeWiFi {
 int state=WL_CONNECTED,reconnects=0,sleep=-1;bool acceptReconnect=true;
 void (*callback)(int,WiFiEventInfo_t)=nullptr;
 void onEvent(void(*fn)(int,WiFiEventInfo_t)){callback=fn;}
 void setAutoReconnect(bool){}
 bool setSleep(int n){sleep=n;return true;}
 void begin(const char*,const char*){}
 int status(){return state;}
 bool reconnect(){++reconnects;return acceptReconnect;}
 void event(int e,int reason=0){WiFiEventInfo_t info;info.wifi_sta_disconnected.reason=reason;if(callback)callback(e,info);}
 void gotIp(){state=WL_CONNECTED;event(ARDUINO_EVENT_WIFI_STA_GOT_IP);}
};
extern FakeWiFi WiFi;
