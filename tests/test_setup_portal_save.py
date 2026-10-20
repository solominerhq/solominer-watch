"""Exercise the actual branded save handler before Wi-FiManager starts connecting."""
from pathlib import Path
import subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
ARDUINO=r'''
#pragma once
#include <string>
#include <cctype>
#include <cstdlib>
class String {
 std::string v;
public:
 String(const char* s=""):v(s?s:""){} String(int n):v(std::to_string(n)){}
 const char* c_str()const{return v.c_str();} size_t length()const{return v.size();}
 char operator[](size_t i)const{return v[i];} long toInt()const{return std::strtol(v.c_str(),nullptr,10);}
 void trim(){size_t a=0,b=v.size();while(a<b&&isspace((unsigned char)v[a]))a++;while(b>a&&isspace((unsigned char)v[b-1]))b--;v=v.substr(a,b-a);}
 bool operator==(const String& b)const{return v==b.v;}bool operator!=(const String& b)const{return v!=b.v;}
 friend String operator+(const String&a,const String&b){return String((a.v+b.v).c_str());}
};
'''
PRE=r'''
#include <cassert>
#include <functional>
#include <map>
#include <vector>
#include "drivers/storage/storage.h"
#include "network/setup_timezones.h"
// Checksum validation is an existing production utility; this adapter controls
// its result so these tests isolate save ordering and transport validation.
bool addressValid=true;bool isValidBtcAddress(const char*){return addressValid;}
std::vector<int> events;
struct Server {
 std::map<std::string,String> args;int status=0;String field;
 String arg(const char* n){return args[n];}
 void send(int code,const char*,const char*){status=code;events.push_back(3);}
};
class SoloMinerWiFiManager {
public:
 Server http;Server* server=&http;TSettings settings;const TSettings* portalSettings_=&settings;
 bool durable=true;std::function<bool()> saved_=[this]{return durable;};
 String token_="session",_ssid="previous",_pass="previous-password";
 bool connect=false;int writes=0;std::function<void()> _presavewificallback=[](){events.push_back(2);};
 String WiFi_SSID(bool){return "Stored Wi-Fi";}String WiFi_psk(bool){return "saved-password";}
 void handleRequest(){}void noCache(){}void doParamSave(){++writes;events.push_back(1);}
 void jsonError(int status,const char* field,const char*){http.status=status;http.field=field;}
 void saveBrandedSettings();
};
void valid(SoloMinerWiFiManager& m){events.clear();addressValid=true;m.http.args={
 {"token","session"},{"s","New Wi-Fi"},{"p","new-password"},{"open","0"},{"keepPassword","0"},
 {"btcAddress","1BoatSLRHtKNngkdXEeobR76b53LETtpyT"},{"PosixTZ","PST8PDT,M3.2.0,M11.1.0"},
 {"ClockFormat","12"},{"Poolurl","public-pool.io"},{"Poolport","21496"},{"Poolpassword","x"},{"WorkerName","SOLOMINER"}};}
void unchanged(SoloMinerWiFiManager& m){assert(!m.connect&&m.writes==0&&m._ssid=="previous"&&m._pass=="previous-password");}
'''
CHECKS=r'''
int main(){
 {SoloMinerWiFiManager m;valid(m);m.saveBrandedSettings();assert(m.connect&&m.writes==1&&m.http.status==200);assert(m._ssid=="New Wi-Fi"&&m._pass=="new-password");assert((events==std::vector<int>{1,2,3}));}
 {SoloMinerWiFiManager m;valid(m);m.durable=false;m.saveBrandedSettings();assert(!m.connect&&m.writes==1&&m.http.status==500&&m._ssid=="previous"&&m._pass=="previous-password");assert((events==std::vector<int>{1}));}
 {SoloMinerWiFiManager m;valid(m);m.http.args["token"]="expired";m.saveBrandedSettings();unchanged(m);assert(m.http.status==403);}
 {SoloMinerWiFiManager m;valid(m);m.http.args["s"]="Stored Wi-Fi";m.http.args["keepPassword"]="1";m.http.args["p"]="";m.saveBrandedSettings();assert(m.connect&&m._pass=="saved-password");}
 {SoloMinerWiFiManager m;valid(m);m.http.args["keepPassword"]="1";m.saveBrandedSettings();unchanged(m);assert(m.http.field=="password");}
 {SoloMinerWiFiManager m;valid(m);m.http.args["open"]="1";m.http.args["p"]="";m.saveBrandedSettings();assert(m.connect&&m._pass=="");}
 {SoloMinerWiFiManager m;valid(m);m.http.args["p"]="short";m.saveBrandedSettings();unchanged(m);}
 {SoloMinerWiFiManager m;valid(m);addressValid=false;m.saveBrandedSettings();unchanged(m);assert(m.http.field=="address");}
 {SoloMinerWiFiManager m;valid(m);m.http.args["PosixTZ"]="";m.saveBrandedSettings();unchanged(m);assert(m.http.field=="timezone");}
 for(const char* port:{"0","65536","21496junk","-1",""}){SoloMinerWiFiManager m;valid(m);m.http.args["Poolport"]=port;m.saveBrandedSettings();unchanged(m);}
 for(const char* zone:{"UTC12","UTC0","UTC-14"}){SoloMinerWiFiManager m;valid(m);m.http.args["PosixTZ"]=zone;m.saveBrandedSettings();assert(m.connect);}
 {SoloMinerWiFiManager m;valid(m);m.http.args["s"]="  spaced network  ";m.saveBrandedSettings();assert(m.connect&&m._ssid=="  spaced network  ");}
}
'''
class BrandedSetupSaveTests(unittest.TestCase):
 def test_validation_persistence_and_saved_password(self):
  code=(ROOT/'src/network/setup_portal.cpp').read_text()
  handler=code[code.index('void SoloMinerWiFiManager::saveBrandedSettings()'):]
  with tempfile.TemporaryDirectory() as temp:
   path=Path(temp);(path/'Arduino.h').write_text(ARDUINO);(path/'test.cpp').write_text(PRE+handler+CHECKS)
   subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(path),'-I'+str(ROOT/'src'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
   subprocess.run([str(path/'test')],check=True,timeout=10)
if __name__=='__main__':unittest.main()
