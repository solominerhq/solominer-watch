"""Execute production persistence code with injected write failures and concurrent edits."""
from pathlib import Path
import os, subprocess, tempfile, unittest
ROOT=Path(__file__).resolve().parents[1]
def run(code):
    with tempfile.TemporaryDirectory() as t:
        p=Path(t);(p/'test.cpp').write_text(code)
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-pthread','-I'+str(ROOT/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True,timeout=10)
class Persistence(unittest.TestCase):
    def test_deferred_save_failure_and_newer_edit(self):
        d=(ROOT/'src/drivers/displays/waveshareAmoled206DisplayDriver.cpp').read_text()
        code=d[d.index('static std::atomic<uint32_t> settingsRevision'):d.index('// Critical power checks')]
        run(r'''
#include <atomic>
#include <mutex>
#include <cstdint>
#include <cassert>
#include "settings/save_retry.h"
uint32_t now=0;uint32_t millis(){return now;}
struct TSettings{int brightness=150;};TSettings Settings;
int lvgl_mutex=1;bool lockAllowed=true;constexpr bool pdTRUE=true;
uint32_t pdMS_TO_TICKS(uint32_t n){return n;}
bool xSemaphoreTake(int,uint32_t){return lockAllowed;}void xSemaphoreGive(int){}
static void markSettingsDirty();
struct Storage{bool fail=true,editDuringSave=false;int writes=0,saved=0;
 bool saveConfig(TSettings* s){++writes;if(editDuringSave){Settings.brightness=230;markSettingsDirty();editDuringSave=false;}if(fail)return false;saved=s->brightness;return true;}
}nvMem;
''' +code+r'''
int main(){markSettingsDirty();now=799;flushPendingSettings();assert(!nvMem.writes);
now=800;flushPendingSettings();assert(nvMem.writes==1 && savedSettingsRevision!=settingsRevision);
now=900;flushPendingSettings();assert(nvMem.writes==1);
now=5800;nvMem.fail=false;nvMem.editDuringSave=true;flushPendingSettings();
assert(nvMem.saved==150 && savedSettingsRevision!=settingsRevision);
now=6600;flushPendingSettings();assert(nvMem.saved==230 && savedSettingsRevision==settingsRevision);
Settings.brightness=60;markSettingsDirty();lockAllowed=false;now+=800;flushPendingSettings();assert(nvMem.saved==230);
lockAllowed=true;nvMem.fail=true;flushPendingSettings();nvMem.fail=false;flushPendingSettings(true);assert(nvMem.saved==60);
}
''')
    def test_stats_migration_corruption_and_commit_failure(self):
        d=(ROOT/'src/mining.cpp').read_text();code=d[d.index('static bool openStats()'):d.index('void runMonitor(')]
        run(r'''
#include <atomic>
#include <mutex>
#include <cassert>
#include <map>
#include <vector>
#include <string>
#include "device/mining_stats.h"
using nvs_handle_t=int;constexpr int ESP_OK=0,NVS_READWRITE=1;
std::map<std::string,std::vector<uint8_t>> disk,pending;bool fail=false;
int nvs_flash_init(){return 0;}int nvs_open(const char*,int,int* h){*h=1;return 0;}
int nvs_get_blob(int,const char* key,void* p,size_t* n){auto it=disk.find(key);if(it==disk.end()||*n<it->second.size())return -1;*n=it->second.size();memcpy(p,it->second.data(),*n);return 0;}
int nvs_get_u32(int h,const char*k,uint32_t*p){size_t n=4;return nvs_get_blob(h,k,p,&n);}
int nvs_get_u64(int h,const char*k,uint64_t*p){size_t n=8;return nvs_get_blob(h,k,p,&n);}
int nvs_set_blob(int,const char*k,const void*p,size_t n){pending[k]=std::vector<uint8_t>((const uint8_t*)p,(const uint8_t*)p+n);return 0;}
int nvs_commit(int){if(fail){pending.clear();return -1;}for(auto&i:pending)disk[i.first]=i.second;pending.clear();return 0;}
uint32_t crc32_reset(){return 0xffffffffu;}uint32_t crc32_add(uint32_t c,const void*p,size_t n){auto b=(const uint8_t*)p;while(n--){c^=*b++;for(int i=0;i<8;++i)c=(c>>1)^((c&1)?0xedb88320u:0);}return c;}uint32_t crc32_finish(uint32_t c){return ~c;}
uint32_t millis(){return 1000;}
int stat_handle=0;std::atomic<bool> statsReady{false},statsSaveFailed{false};std::atomic<uint32_t> statsLastAttempt{0};std::mutex statsIoMutex,statsValueMutex;
double best_diff=0;uint32_t Mhashes=0,hashes=0,shares=0,valids=0,templates=0,totalKHashes=0,elapsedKHs=0;uint64_t upTime=0;
template<class T>void put(const char*k,T v){nvs_set_blob(1,k,&v,sizeof(v));nvs_commit(1);}
''' +code+r'''
int main(){checkpointMiningStats();assert(disk.empty());restoreStat();assert(!Mhashes&&!shares&&!valids&&!upTime);
mining_stats::Snapshot old;old.best=42;old.mega=900;old.shares=100;old.blocks=2;old.jobs=50;old.uptime=600;
put("best_diff",old.best);put("Mhashes",old.mega);put("shares",old.shares);put("valids",old.blocks);put("templates",old.jobs);put("upTime",old.uptime);
uint32_t c=crc32_reset();c=crc32_add(c,&old.best,8);c=crc32_add(c,&old.mega,4);c=crc32_add(c,&old.shares,4);c=crc32_add(c,&old.blocks,4);c=crc32_add(c,&old.jobs,4);c=crc32_add(c,&old.uptime,8);put("crc32",crc32_finish(c));
restoreStat();assert(Mhashes==900&&best_diff==42&&valids==2&&!upTime);
hashes=123456;assert(saveStat());Mhashes=901;fail=true;assert(!saveStat());assert(statsSaveFailed);fail=false;
restoreStat();assert(Mhashes==900&&hashes==123456);Mhashes=902;checkpointMiningStats();restoreStat();assert(Mhashes==902);
disk["snapshot_v1"][3]^=1;restoreStat();assert(Mhashes==900); // valid legacy fallback, not random values
resetStat();restoreStat();assert(!Mhashes&&!hashes&&!valids);
disk.clear();put("shares",uint32_t(999));restoreStat();assert(!shares&&!valids&&!Mhashes); // incomplete legacy never used
}
''')
