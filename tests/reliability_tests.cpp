#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <initializer_list>
#include "device/clock_validation.h"
#include "device/mining_stats.h"
#include "settings/save_retry.h"
#include "ui/battery_notice.h"
#include "ui/mining_status.h"
int main(){
 using namespace clock_validation;
 assert(valid(2028,2,29,23,59,59));assert(!valid(2026,2,29,0,0,0));
 for(int m:{-1,0,13,31})assert(!valid(2026,m,1,0,0,0));
 assert(!valid(2026,4,31,1,1,1));assert(!valid(2026,10,19,24,0,0));
 uint8_t rtc[]={0x59,0x58,0x23,0x19,1,0x10,0x26};assert(registers(rtc));
 rtc[0]|=0x80;assert(!registers(rtc));rtc[0]=0x5a;assert(!registers(rtc));
 rtc[0]=0;rtc[5]=0;assert(!registers(rtc));rtc[5]=0x10;rtc[4]=7;assert(!registers(rtc));
 settings::SaveRetry retry;assert(!retry.due(799,0,false));assert(retry.due(800,0,false));
 retry.result(800,false);assert(!retry.due(5799,0,false));assert(retry.due(5800,0,false));
 assert(retry.due(801,0,true));retry.result(0xfffffff0u,false);
 assert(!retry.due(100,0xffff0000u,false));assert(retry.due(5000,0xffff0000u,false));
 retry.result(5000,true);assert(retry.due(5001,0,false));
 ui::BatteryNotice notice;notice.sample(-1,true,0xfffffff0u);
 assert(!notice.unavailable(100));assert(notice.unavailable(60000));
 assert(!notice.takeUnavailable(false,60000));assert(notice.takeUnavailable(true,60000));
 assert(!notice.takeUnavailable(true,70000));notice.sample(50,true,70001);assert(!notice.unavailable(90000));
 notice.sample(10,false,80000);assert(!notice.takeLow(false));assert(notice.takeLow(true));
 notice.sample(9,false,90000);assert(!notice.takeLow(true));notice.sample(9,true,100000);
 notice.sample(8,false,110000);assert(!notice.takeLow(true)); // USB bounce does not reset the warning
 notice.sample(50,true,120000);notice.sample(10,false,130000);assert(notice.takeLow(true));
 mining_stats::Snapshot s;s.best=12.345;s.mega=999;s.shares=20;s.blocks=1;s.jobs=15;s.uptime=123456;s.remainder=456789;
 uint8_t b[mining_stats::Bytes];mining_stats::encode(s,b);mining_stats::Snapshot decoded;
 assert(mining_stats::decode(b,sizeof(b),decoded));assert(decoded.best==s.best && decoded.mega==999 && decoded.remainder==456789 && decoded.blocks==1);
 for(size_t i=0;i<sizeof(b);++i){b[i]^=1;assert(!mining_stats::decode(b,sizeof(b),decoded));b[i]^=1;}
 assert(!mining_stats::decode(b,sizeof(b)-1,decoded));s.best=std::numeric_limits<double>::quiet_NaN();mining_stats::encode(s,b);assert(!mining_stats::decode(b,sizeof(b),decoded));
 auto status=ui::connectionStatus(false,0,false,true,0,false,network::WifiStage::Waiting,true,network::problemForReason(201));
 assert(!strcmp(status.clock,"Connecting to Wi-Fi") && !strcmp(status.detail,"Network not found"));
 status=ui::connectionStatus(false,0,false,true,0,false,network::WifiStage::Connecting,false,network::problemForReason(202));assert(!status.detail[0]);
 assert(network::problemForReason(2)==network::WifiProblem::Unknown);
 puts("PASS: clock validation, save retries, battery notices, stats record integrity and connection guidance");
}
