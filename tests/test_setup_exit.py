"""Run the production provisioning loop and post-exit policy with host adapters."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
PRE=r'''
#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include "network/portal_session.h"
#define SERIAL_PRINTLN(...) ((void)0)
#define WL_CONNECTED 3
#define SECOND_MS 1000
enum {NM_waitingConfig,NM_Connecting};
struct {int NerdStatus=NM_waitingConfig;} mMonitor;
bool shouldSaveConfig=false,portalConfigSaved=false,wifiSetupSubmitted=false;
bool committed=false; unsigned off=0,restart=0,idles=0;
std::atomic<bool> setupExitRequested{false};
struct {int state=0;int status(){return state;}} WiFi;
struct {void restart(){++::restart;}} ESP;
struct Target {char ssid[33];} target={"Saved network"};
Target wifiTargetSnapshot(){return target;}
void delay(int){++idles;}
void powerOffDevice(){++off;}
struct Manager {
 bool active=true,web=false,busy=false; unsigned stops=0,steps=0;
 std::function<bool()> processEvent;
 bool getConfigPortalActive(){return active;}
 bool getWebPortalActive(){return web;}
 void stopWebPortal(){assert(!busy);web=false;}
 void stopConfigPortal(){assert(!busy);active=false;++stops;}
 bool process(){busy=true;++steps;bool connected=processEvent();busy=false;return connected;}
} wm;
void reset(){
 wm=Manager{};target=Target{"Saved network"};mMonitor.NerdStatus=NM_waitingConfig;
 shouldSaveConfig=portalConfigSaved=wifiSetupSubmitted=committed=false;
 setupExitRequested=false;off=restart=idles=0;WiFi.state=0;
}
'''
CHECKS=r'''
int main(){
 reset(); setupExitRequested=true; run();
 assert(wm.stops==1&&wm.steps==0&&off==0&&restart==0&&mMonitor.NerdStatus==NM_Connecting);
 assert(!strcmp(target.ssid,"Saved network"));
 reset(); target.ssid[0]=0; setupExitRequested=true; run();
 assert(wm.stops==1&&off==0&&restart==0&&mMonitor.NerdStatus==NM_Connecting);
 // The display queues exit while HTTP owns a Save. Stop happens after commit.
 reset(); wm.processEvent=[](){
   setupExitRequested=true;shouldSaveConfig=portalConfigSaved=wifiSetupSubmitted=true;
   assert(wm.stops==0);committed=true;return false;
 }; run();
 assert(committed&&wm.stops==1&&restart==0&&!wifiSetupSubmitted);
 // A connection and exit in the same iteration must not lose the request.
 reset(); wm.processEvent=[](){
   shouldSaveConfig=portalConfigSaved=committed=true;setupExitRequested=true;
   wm.active=false;WiFi.state=WL_CONNECTED;return true;
 };run();assert(committed&&restart==0&&off==0);
 // Successful Save without exit retains the existing restart behavior.
 reset();wm.processEvent=[](){shouldSaveConfig=portalConfigSaved=committed=true;wm.active=false;WiFi.state=WL_CONNECTED;return true;};
 run();assert(committed&&restart==1&&off==0);
 // An abandoned setup timeout resumes stored credentials; an unconfigured
 // timeout still uses the existing shutdown/USB charging fallback.
 reset();wm.processEvent=[](){wm.active=false;return false;};run();assert(off==0&&mMonitor.NerdStatus==NM_Connecting);
 reset();target.ssid[0]=0;wm.processEvent=[](){wm.active=false;return false;};run();assert(off==1);
}
'''
class SetupExitTests(unittest.TestCase):
    def test_exit_waits_for_save_and_preserves_timeout_policy(self):
        source=(ROOT/'src/wManager.cpp').read_text()
        a=source.index('        const bool exitedByUser = network::runProvisioningPortal')
        b=source.index('\n    }\n    else\n    {\n        // Normal boot',a)
        code=PRE+'\nvoid run(){\n'+source[a:b]+'\n}\n'+CHECKS
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp);(tmp/'test.cpp').write_text(code)
            subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
            subprocess.run([str(tmp/'test')],check=True,timeout=10)

if __name__=='__main__':unittest.main()
