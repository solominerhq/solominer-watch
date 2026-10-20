#include "device/block_record.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace block_record;
struct Backend {
    std::vector<uint8_t> disk;bool fail=false;int writes=0;
    bool load(uint8_t* p,size_t& n){if(disk.empty() || n<disk.size())return false;n=disk.size();memcpy(p,disk.data(),n);return true;}
    bool save(const uint8_t* p,size_t n){++writes;if(fail)return false;disk.assign(p,p+n);return true;}
};
int main(){
    const char* hash="0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a5";
    const char* next="0000000000000000000320283a032748cef8227873ff4872689bf23f1cda83a6";
    Backend disk;Store<Backend> first(disk);first.init();assert(!first.snapshot().valid());
    assert(!first.accept(1,2,3,"bad",1));assert(disk.writes==0);
    assert(first.accept(840000,1710000000,312500000,hash,7));
    assert(!first.dirty() && disk.writes==1);
    Store<Backend> boot(disk);boot.init();auto saved=boot.snapshot();
    assert(saved.valid() && saved.unread && saved.count==7 && saved.height==840000);
    assert(saved.subsidy==312500000 && saved.time==1710000000 && !strcmp(saved.hash,hash));
    // Duplicate acceptance is idempotent, including after restart.
    boot.accept(840000,1710000000,312500000,hash,8);assert(boot.snapshot().count==7 && disk.writes==1);
    boot.review(6);assert(boot.snapshot().unread && !boot.dirty());
    boot.review(7);assert(!boot.snapshot().unread && boot.dirty());assert(boot.flush());
    Store<Backend> reviewedBoot(disk);reviewedBoot.init();assert(!reviewedBoot.snapshot().unread);
    // Failed commit keeps last durable record and retries the new one.
    disk.fail=true;boot.accept(840001,1710001000,312500000,next,8);assert(boot.dirty() && boot.snapshot().count==8);
    Store<Backend> interrupted(disk);interrupted.init();assert(interrupted.snapshot().count==7);
    boot.review(7);assert(boot.snapshot().unread); // Old UI must not acknowledge new event.
    disk.fail=false;assert(boot.flush());Store<Backend> retry(disk);retry.init();assert(retry.snapshot().count==8);
    // Corruption, schema mismatch and truncated records never fabricate finds.
    auto good=disk.disk;
    for(size_t i=0;i<good.size();++i){disk.disk=good;disk.disk[i]^=1;Store<Backend> bad(disk);bad.init();assert(!bad.snapshot().valid());}
    disk.disk=good;disk.disk.pop_back();Store<Backend> shortRecord(disk);shortRecord.init();assert(!shortRecord.snapshot().valid());
    // Missing height/reward may occur on a malformed pool job: preserve the hash.
    disk.disk.clear();Store<Backend> partial(disk);partial.accept(0,0,0,hash,1);assert(partial.snapshot().valid());
    puts("PASS: block persistence, restart, unread acknowledgment, idempotence, failed-write retry and corrupted-record rejection");
}
