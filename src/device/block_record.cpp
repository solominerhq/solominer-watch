#include "block_record.h"
#include <Arduino.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <mutex>
#include "seriallog.h"
namespace block_record {
namespace {
struct NvsBackend {
    nvs_handle_t handle=0;bool opened=false;
    bool open() {
        if(opened)return true;
        // Never erase a partition here: missing/unreadable storage must not
        // destroy settings or previously saved records.
        if(nvs_flash_init()!=ESP_OK)return false;
        opened=nvs_open("block_record",NVS_READWRITE,&handle)==ESP_OK;return opened;
    }
    bool load(uint8_t* p,size_t& n) { return open() && nvs_get_blob(handle,"latest",p,&n)==ESP_OK; }
    bool save(const uint8_t* p,size_t n) {
        return open() && nvs_set_blob(handle,"latest",p,n)==ESP_OK && nvs_commit(handle)==ESP_OK;
    }
} backend;
Store<NvsBackend> store(backend);
std::mutex mutex;
uint32_t lastAttempt=0,retryDelay=1000;
}
void initialize(){std::lock_guard<std::mutex> lock(mutex);store.init();}
Record snapshot(){std::lock_guard<std::mutex> lock(mutex);return store.snapshot();}
uint32_t accept(uint32_t height,uint32_t time,uint64_t subsidy,const char* hash,uint32_t count) {
    std::lock_guard<std::mutex> lock(mutex);
    if(!store.accept(height,time,subsidy,hash,count))return count;
    if(store.dirty())SERIAL_PRINTLN("[BLOCK] Record save failed; retry pending");
    return store.snapshot().count;
}
void review(uint32_t count){std::lock_guard<std::mutex> lock(mutex);store.review(count);}
void service(){
    std::lock_guard<std::mutex> lock(mutex);
    if(!store.dirty() || uint32_t(millis()-lastAttempt)<retryDelay)return;
    lastAttempt=millis();retryDelay=store.flush() ? 1000 : 30000;
}
}
