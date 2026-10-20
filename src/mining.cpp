#include <atomic>
#include "device/block_record.h"
#include "device/mining_stats.h"
#include "device/sleep_control.h"
#include "diagnostics/diagnostics.h"
#include "device/mining_control.h"
#include "device/device_power.h"
#include "network/wifi_connection.h"
#include "network/connection_policy.h"
#include "network/authorization.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <nvs_flash.h>
#include <nvs.h>
//#include "ShaTests/nerdSHA256.h"
#include "ShaTests/nerdSHA256plus.h"
#include "stratum.h"
#include "mining.h"
#include "seriallog.h"
#include "events.h"
#include "utils.h"
#include "monitor.h"
#include "timeconst.h"
#include "drivers/displays/display.h"
#include "drivers/storage/storage.h"
#include <mutex>
#include <list>
#include <map>
#include "mbedtls/sha256.h"
#include "i2c_master.h"
#include "freertos/semphr.h"

// LVGL mutex - defined in displayDriver.cpp
extern SemaphoreHandle_t lvgl_mutex;

// Screen state check - defined in displayDriver.cpp
extern bool isScreenActive(void);

// Immediate redraw flag - set by gesture handler on swipe
extern std::atomic<bool> needsImmediateRedraw;

//10 Jobs per second
#define NONCE_PER_JOB_SW 4096
#define NONCE_PER_JOB_HW 16*1024

//#define I2C_SLAVE

//#define SHA256_VALIDATE
//#define RANDOM_NONCE
#define RANDOM_NONCE_MASK 0xFFFFC000

#ifdef HARDWARE_SHA265
#include <sha/sha_dma.h>
#include <hal/sha_hal.h>
#include <hal/sha_ll.h>

#if defined(CONFIG_IDF_TARGET_ESP32)
#include <sha/sha_parallel_engine.h>
#endif

#endif

nvs_handle_t stat_handle=0;
static std::atomic<bool> statsReady{false};
static std::mutex statsIoMutex, statsValueMutex;
static std::atomic<uint32_t> statsLastAttempt{0};
static std::atomic<bool> statsSaveFailed{false};

uint32_t templates = 0;
volatile uint32_t hashes = 0;  // volatile: written by miner tasks, read by display + stratum worker
uint32_t Mhashes = 0;
uint32_t totalKHashes = 0;
uint32_t elapsedKHs = 0;
uint64_t upTime = 0;

volatile uint32_t shares; // increase if blockhash has 32 bits of zeroes
volatile uint32_t valids; // increased if blockhash <= target

// Track best diff
double best_diff = 0.0;

// BIP34: the coinbase scriptSig begins with a push of the block height (little
// endian). coinb1 (hex) holds the start of the coinbase tx:
//   version(4) + in_count(1) + prevout(36) + scriptSig_len(1) + scriptSig...
// = 42 bytes (84 hex chars) before the scriptSig, whose first byte is the
// height push length N, followed by N little-endian height bytes.
static uint32_t parseBlockHeightFromCoinb1(const String& coinb1) {
    const int SCRIPTSIG = 84;                    // hex offset of scriptSig start
    if ((int)coinb1.length() < SCRIPTSIG + 2) return 0;
    auto hx = [&](int i)->int{
        char c = coinb1.charAt(i);
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    auto byteAt = [&](int hexOff)->int{
        int hi = hx(hexOff), lo = hx(hexOff + 1);
        if (hi < 0 || lo < 0) return -1;
        return (hi << 4) | lo;
    };
    int n = byteAt(SCRIPTSIG);                    // push length (bytes of height)
    if (n < 1 || n > 4) return 0;                 // sane heights fit in 1-4 bytes
    if ((int)coinb1.length() < SCRIPTSIG + 2 + n * 2) return 0;
    uint32_t height = 0;
    for (int i = 0; i < n; i++) {                 // little-endian
        int b = byteAt(SCRIPTSIG + 2 + i * 2);
        if (b < 0) return 0;
        height |= ((uint32_t)b) << (8 * i);
    }
    return height;
}

// Bitcoin block subsidy (satoshis) from height: 50 BTC, halving every 210,000
// blocks. Returns 0 after the subsidy rounds to zero (~year 2140).
static uint64_t blockSubsidySats(uint32_t height) {
    uint32_t halvings = height / 210000u;
    if (halvings >= 64) return 0;
    return (uint64_t)5000000000ULL >> halvings;   // 50e8 sats
}

// Variables to hold data from custom textboxes
//Track mining stats in non volatile memory
extern TSettings Settings;

IPAddress serverIP(1, 1, 1, 1); //Temporally save poolIPaddres

// Pool connection phase (see monitor.h) — read by the clock screen so the user
// can see what's happening between WiFi and mining.
std::atomic<int>     g_poolPhase{POOL_PHASE_IDLE};
std::atomic<uint8_t> g_poolRetryCount{0};

//Global work data
static WiFiClient client;
static miner_data mMiner; //Global miner data (Create a miner class TODO)
mining_subscribe mWorker;
mining_job mJob;
monitor_data mMonitor;
static bool volatile isMinerSuscribed = false;
unsigned long mLastTXtoPool = millis();

// Bound unexpected power-loss exposure without writing on each hash.
static constexpr uint32_t STATS_SAVE_MS=5*60*1000;

bool checkPoolConnection(void) {
  
  if (client.connected()) {
    return true;
  }
  
  isMinerSuscribed = false;

  SERIAL_PRINTLN("Client not connected, trying to connect...");
  
  //Resolve pool DNS - always try to resolve, even if we have cached IP
  //This fixes DNS resolution failures that can happen after WiFi reconnects
  IPAddress resolvedIP;
  if(WiFi.hostByName(Settings.PoolAddress.c_str(), resolvedIP)) {
    // DNS resolution succeeded
    if(resolvedIP != IPAddress(0,0,0,0)) {
      serverIP = resolvedIP;
      SERIAL_PRINTF("Resolved DNS got: %s\n", serverIP.toString().c_str());
    } else {
      SERIAL_PRINTLN("DNS returned 0.0.0.0 - using cached IP if available");
      if(serverIP == IPAddress(1,1,1,1) || serverIP == IPAddress(0,0,0,0)) {
        SERIAL_PRINTLN("No valid IP available, cannot connect");
        return false;
      }
    }
  } else {
    // DNS resolution failed
    SERIAL_PRINTLN("DNS resolution failed - using cached IP if available");
    if(serverIP == IPAddress(1,1,1,1) || serverIP == IPAddress(0,0,0,0)) {
      SERIAL_PRINTLN("No valid IP available, cannot connect");
      return false;
    }
  }

  //Try connecting pool IP
  if (!client.connect(serverIP, Settings.PoolPort)) {
    SERIAL_PRINTLN("Imposible to connect to : " + Settings.PoolAddress + " at " + serverIP.toString());
    return false;
  }

  SERIAL_PRINTLN("Successfully connected to pool!");
  return true;
}

// Liveness measures the network, never whether a power/thermal policy is
// intentionally pausing hashing. Fresh-work timeout is handled by PoolSession.
bool checkPoolKeepalive(uint32_t now) {
  if (!policy::elapsed(now, mLastTXtoPool, KEEPALIVE_TIME_ms)) return true;
  mLastTXtoPool = now;
  return tx_suggest_difficulty(client, DEFAULT_DIFFICULTY);
}

struct JobRequest
{
  uint32_t id;
  uint32_t nonce_start;
  uint32_t nonce_count;
  double difficulty;
  uint8_t sha_buffer[128];
  uint32_t midstate[8];
  uint32_t bake[16];
};

struct JobResult
{
  uint32_t id;
  uint32_t nonce;
  uint32_t nonce_count;
  double difficulty;
  uint8_t hash[32];
};

static std::mutex s_job_mutex;
std::list<std::shared_ptr<JobRequest>> s_job_request_list_sw;
#ifdef HARDWARE_SHA265
std::list<std::shared_ptr<JobRequest>> s_job_request_list_hw;
#endif
std::list<std::shared_ptr<JobResult>> s_job_result_list;
static std::atomic<uint32_t> s_working_current_job_id{0xFFFFFFFFu};

static void JobPush(std::list<std::shared_ptr<JobRequest>> &job_list,  uint32_t id, uint32_t nonce_start, uint32_t nonce_count, double difficulty,
                    const uint8_t* sha_buffer, const uint32_t* midstate, const uint32_t* bake)
{
  std::shared_ptr<JobRequest> job = std::make_shared<JobRequest>();
  job->id = id;
  job->nonce_start = nonce_start;
  job->nonce_count = nonce_count;
  job->difficulty = difficulty;
  memcpy(job->sha_buffer, sha_buffer, sizeof(job->sha_buffer));
  memcpy(job->midstate, midstate, sizeof(job->midstate));
  memcpy(job->bake, bake, sizeof(job->bake));
  job_list.push_back(job);
}

struct Submition
{
  double diff;
  bool is32bit;
  bool isValid;
  // Snapshot of the winning block's identity, captured at submit time so the
  // celebration shows the EXACT block we solved (not whatever job is current
  // when the pool's acceptance arrives a few seconds later).
  uint32_t height;
  uint32_t ntime;
  char     hash[65];   // big-endian (block-explorer order) hex, NUL-terminated
};

static void MiningJobStop(uint32_t &job_pool, std::map<uint32_t, std::shared_ptr<Submition>> & submition_map)
{
  {
    std::lock_guard<std::mutex> lock(s_job_mutex);
    s_job_result_list.clear();
    s_job_request_list_sw.clear();
    #ifdef HARDWARE_SHA265
    s_job_request_list_hw.clear();
    #endif
  }
  s_working_current_job_id = 0xFFFFFFFFu;
  job_pool = 0xFFFFFFFF;
  submition_map.clear();
}

#ifdef RANDOM_NONCE
uint64_t s_random_state = 1;
static uint32_t RandomGet()
{
    s_random_state += 0x9E3779B97F4A7C15ull;
    uint64_t z = s_random_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

#endif

void runStratumWorker(void *name) {

// TEST: https://bitcoin.stackexchange.com/questions/22929/full-example-data-for-scrypt-stratum-client

  SERIAL_PRINTLN("");
  SERIAL_PRINTF("\n[WORKER] Started. Running %s on core %d\n", (char *)name, xPortGetCoreID());

  #ifdef DEBUG_MEMORY
  SERIAL_PRINTF("### [Total Heap / Free heap / Min free heap]: %d / %d / %d \n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
  #endif

  std::map<uint32_t, std::shared_ptr<Submition>> s_submition_map;

  // Block re-send guard. A solved BLOCK is worth a fortune and must reach the
  // pool on THIS connection (a fresh connection won't recognize the job id), so
  // fire-and-forget is unacceptable. We keep the exact submit frame and re-send
  // it on the live connection until the pool acknowledges it. Its lifetime is
  // tied to the submission map entry: when STRATUM_SUCCESS/reject erases the
  // entry, or a job reset clears the map, re-sending stops automatically.
  struct { bool active; unsigned long id; char payload[BUFFER]; uint32_t lastMs; int count; }
    s_blockResend = { false, 0, {0}, 0, 0 };
  const uint32_t BLOCK_RESEND_MS  = 4000;   // wait this long for an ACK before re-sending
  const int      BLOCK_RESEND_MAX = 6;      // ~24s of re-tries on the live socket

#ifdef I2C_SLAVE
  std::vector<uint8_t> i2c_slave_vector;

  //scan for i2c slaves
  if (i2c_master_start() == 0)
    i2c_slave_vector = i2c_master_scan(0x0, 0x80);
  SERIAL_PRINTF("Found %d slave workers\n", i2c_slave_vector.size());
  if (!i2c_slave_vector.empty())
  {
    SERIAL_PRINT("  Workers: ");
    for (size_t n = 0; n < i2c_slave_vector.size(); ++n)
      SERIAL_PRINTF("0x%02X,", (uint32_t)i2c_slave_vector[n]);
    SERIAL_PRINTLN("");
  }
#endif

  // connect to pool
  double currentPoolDifficulty = DEFAULT_DIFFICULTY;
  uint32_t nonce_pool = 0;
  uint32_t job_pool = 0xFFFFFFFF;
  network::PoolSession session;
  network::RetrySchedule poolRetry;
  String pendingJob;

  // Block header working data
  uint32_t hw_midstate[8];
  uint32_t diget_mid[8];
  uint32_t bake[16];
  #if defined(CONFIG_IDF_TARGET_ESP32)
  uint8_t sha_buffer_swap[128];
  #endif

  uint32_t jobSequence=0; // Never reuse work identities across pool reconnects.
  bool saverWasIdle=false;
  while(true) {
    if(device::batterySaverIdle()){
      if(!saverWasIdle){
        MiningJobStop(job_pool,s_submition_map);client.stop();
        isMinerSuscribed=false;pendingJob="";poolRetry.reset();
        session=network::PoolSession();
      }
      saverWasIdle=true;g_poolPhase=POOL_PHASE_IDLE;
      // Keep the main UI pages; this is intentional inactivity, not setup/error.
      mMonitor.NerdStatus=NM_Connecting;
      network::serviceIdle();
      device::lightSleepCheckpoint(device::SleepRole::Stratum);
      vTaskDelay(pdMS_TO_TICKS(100));continue;
    }
    saverWasIdle=false;
    // Only this worker services runtime Wi-Fi. Screen and power events merely
    // publish requests; they never begin/disconnect a network connection.
    if (!network::service()) {
      mMonitor.NerdStatus = NM_Connecting;
      g_poolPhase = POOL_PHASE_IDLE;
      MiningJobStop(job_pool, s_submition_map);
      client.stop();
      isMinerSuscribed = false;
      pendingJob = "";
      poolRetry.reset();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Never mine without a CHECKSUM-VALID wallet address. A mistyped address
    // (wrong checksum) would authorize with a username the customer doesn't
    // control, and any block reward would be lost forever. Refuse to mine and
    // show the setup screen instead. isValidBtcAddress caches its result, so
    // this is cheap in the loop.
    if (!isValidBtcAddress(Settings.BtcWallet)) {
      static bool walletWarned = false;
      if (!walletWarned) {
        SERIAL_PRINTLN("[WORKER] No valid BTC wallet configured (empty or bad checksum) - mining paused. Set a valid wallet in Setup.");
        walletWarned = true;
      }
      mMonitor.NerdStatus = NM_waitingConfig;
      MiningJobStop(job_pool, s_submition_map);
      if (client.connected()) client.stop();
      isMinerSuscribed = false;
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }

    if (!client.connected()) {
      MiningJobStop(job_pool, s_submition_map);
      isMinerSuscribed = false;
      pendingJob = "";
      if (!poolRetry.due(millis())) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
      g_poolPhase = POOL_PHASE_CONNECTING;
      poolRetry.tried(millis());
      if (!checkPoolConnection()) {
        if (g_poolRetryCount < 255) ++g_poolRetryCount;
        g_poolPhase = POOL_PHASE_RETRYING;
        continue;
      }
    }

    if(!isMinerSuscribed)
    {
      pendingJob = "";
      g_poolPhase = POOL_PHASE_SUBSCRIBING;   // socket up; doing the Stratum handshake
      //Stop miner current jobs
      mWorker = init_mining_subscribe();

      // STEP 1: Pool server connection (SUBSCRIBE)
      if(!tx_mining_subscribe(client, mWorker)) {
        client.stop();
        MiningJobStop(job_pool, s_submition_map);
        continue;
      }
      
      // Build worker name: address.worker (e.g. bc1q...xyz.Watch1)
      // If no worker name set, just use the BTC address alone.
      // Use snprintf to prevent buffer overflow (wName is 120 bytes).
      // Worker name must also be JSON-safe; if it contains characters that
      // would break the stratum frames, fall back to the wallet alone.
      bool workerNameSane = strlen(Settings.WorkerName) > 0;
      for (const char* p = Settings.WorkerName; workerNameSane && *p; ++p) {
          if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_')
              workerNameSane = false;
      }
      if (workerNameSane) {
          snprintf(mWorker.wName, sizeof(mWorker.wName), "%s.%s",
                   Settings.BtcWallet, Settings.WorkerName);
      } else {
          if (strlen(Settings.WorkerName) > 0)
              SERIAL_PRINTLN("[WORKER] Worker name has unsafe characters - using wallet only");
          snprintf(mWorker.wName, sizeof(mWorker.wName), "%s", Settings.BtcWallet);
      }
      snprintf(mWorker.wPass, sizeof(mWorker.wPass), "%s", Settings.PoolPassword);  // bounded copy
      // Do not dispatch work until this request receives an explicit true result.
      unsigned long authId = 0;
      if (!tx_mining_auth(client, mWorker.wName, mWorker.wPass, authId)) {
        client.stop();
        g_poolPhase = POOL_PHASE_RETRYING;
        continue;
      }
      session.begin(authId, millis());
      tx_suggest_difficulty(client, currentPoolDifficulty);
      isMinerSuscribed = true;
      g_poolPhase = POOL_PHASE_AUTHORIZING;
      mLastTXtoPool = millis();
    }

    if (session.authExpired(millis()) || session.stale(millis()) || !checkPoolKeepalive(millis())) {
      client.stop();
      isMinerSuscribed = false;
      g_poolPhase = POOL_PHASE_RETRYING;
      MiningJobStop(job_pool, s_submition_map);
      continue;
    }

    //Read pending messages from pool
    while (!device::batterySaverIdle() && client.connected() && (client.available() || (session.authorized() && pendingJob.length())))
    {
      // Some pools send their initial job before authorization. Keep the latest
      // one without hashing it, then process it immediately after acceptance.
      String line;
      if (session.authorized() && pendingJob.length()) { line = pendingJob; pendingJob = ""; }
      else line = client.readStringUntil('\n');
      if (!session.authorized()) {
        const auto reply = network::authorizationReply(line.c_str(), session.requestId());
        if (reply != network::AuthReply::Unrelated) {
          const bool accepted = reply == network::AuthReply::Accepted;
          session.reply(accepted);
          if (!accepted) {
            client.stop();
            isMinerSuscribed = false;
            g_poolPhase = POOL_PHASE_AUTH_REJECTED;
            MiningJobStop(job_pool, s_submition_map);
            pendingJob = "";
            break;
          }
          g_poolRetryCount = 0;
          poolRetry.reset();
          g_poolPhase = POOL_PHASE_WAITING_JOB;
          continue;
        }
      }
      //SERIAL_PRINTLN("  Received message from pool");
      stratum_method result = parse_mining_method(line);
      if (result == MINING_NOTIFY && !session.authorized()) {
        pendingJob = line;
        continue;
      }
      switch (result)
      {
          case MINING_NOTIFY:         if(parse_mining_notify(line, mJob))
                                      {
                                          {
                                            std::lock_guard<std::mutex> lock(s_job_mutex);
                                            s_job_request_list_sw.clear();
                                            #ifdef HARDWARE_SHA265
                                            s_job_request_list_hw.clear();
                                            #endif
                                          }
                                          //Increse templates readed
                                          { std::lock_guard<std::mutex> statsLock(statsValueMutex); ++templates; }
                                          onNewJobReceived();
                                          job_pool=++jobSequence;
                                          if(job_pool==0xFFFFFFFFu)job_pool=++jobSequence;
                                          s_working_current_job_id = job_pool; //Terminate current job in thread

                                          session.receivedJob(millis());
                                          mLastTXtoPool = millis();
                                          g_poolPhase = POOL_PHASE_MINING;   // work is flowing — real "mining" state for the UI

                                          {
                                            std::lock_guard<std::mutex> lock(s_job_mutex);
                                            std::lock_guard<std::mutex> statsLock(statsValueMutex);
                                            uint32_t mh = hashes/1000000;
                                            Mhashes += mh;
                                            hashes -= mh*1000000;
                                          }

                                          //Prepare data for new jobs
                                          mMiner=calculateMiningData(mWorker, mJob);

                                          memset(mMiner.bytearray_blockheader+80, 0, 128-80);
                                          mMiner.bytearray_blockheader[80] = 0x80;
                                          mMiner.bytearray_blockheader[126] = 0x02;
                                          mMiner.bytearray_blockheader[127] = 0x80;

                                          nerd_mids(diget_mid, mMiner.bytearray_blockheader);
                                          nerd_sha256_bake(diget_mid, mMiner.bytearray_blockheader+64, bake);

                                          #ifdef HARDWARE_SHA265
                                          #if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
                                            esp_sha_acquire_hardware();
                                            sha_hal_hash_block(SHA2_256,  mMiner.bytearray_blockheader, 64/4, true);
                                            sha_hal_read_digest(SHA2_256, hw_midstate);
                                            esp_sha_release_hardware();
                                          #endif
                                          #endif

                                          #if defined(CONFIG_IDF_TARGET_ESP32)
                                          for (int i = 0; i < 32; ++i)
                                            ((uint32_t*)sha_buffer_swap)[i] = __builtin_bswap32(((const uint32_t*)(mMiner.bytearray_blockheader))[i]);
                                          #endif

                                          #ifdef RANDOM_NONCE
                                          nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
                                          #else
                                            #ifdef I2C_SLAVE
                                            if (!i2c_slave_vector.empty())
                                              nonce_pool = 0x10000000;
                                            else
                                            #endif
                                              nonce_pool = 0xDA54E700;  //nonce 0x00000000 is not possible, start from some random nonce
                                          #endif
                                          

                                          {
                                            std::lock_guard<std::mutex> lock(s_job_mutex);
                                            for (int i = 0; i < 4; ++ i)
                                            {
                                              #if 1
                                              if (!g_singleCoreMode && !g_thermalSingleCore) {
                                              JobPush( s_job_request_list_sw, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake);
                                              #ifdef RANDOM_NONCE
                                              nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
                                              #else
                                              nonce_pool += NONCE_PER_JOB_SW;
                                              #endif
                                              }
                                              #endif
                                              #ifdef HARDWARE_SHA265
                                                #if defined(CONFIG_IDF_TARGET_ESP32)
                                                  JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, hw_midstate, bake);
                                                #else
                                                  JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake);
                                                #endif
                                              #ifdef RANDOM_NONCE
                                              nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
                                              #else
                                              nonce_pool += NONCE_PER_JOB_HW;
                                              #endif
                                              #endif
                                            }
                                          }
                                          #ifdef I2C_SLAVE
                                          //Nonce for nonce_pool starts from 0x10000000
                                          //For i2c slave we give nonces from 0x20000000, that is 0x10000000 nonces per slave
                                          i2c_feed_slaves(i2c_slave_vector, job_pool & 0xFF, 0x20, currentPoolDifficulty, mMiner.bytearray_blockheader);
                                          #endif
                                      } else
                                      {
                                        diagnostics::event(diagnostics::Event::PoolParseError);
                                        SERIAL_PRINTLN("Parsing error, need restart");
                                        client.stop();
                                        isMinerSuscribed=false;
                                        MiningJobStop(job_pool, s_submition_map);
                                      }
                                      break;
          case MINING_SET_DIFFICULTY: parse_mining_set_difficulty(line, currentPoolDifficulty);
                                      break;
          case STRATUM_SUCCESS:       {
                                        unsigned long id = parse_extract_id(line);
                                        auto itt = s_submition_map.find(id);
                                        if (itt != s_submition_map.end())
                                        {
                                          std::lock_guard<std::mutex> statsLock(statsValueMutex);
                                          if (itt->second->diff > best_diff)
                                            best_diff = itt->second->diff;
                                          diagnostics::event(diagnostics::Event::Accepted);
                                          if (itt->second->is32bit)
                                          {
                                            shares++;
                                            onShareAccepted(itt->second->diff);
                                          }
                                          if (itt->second->isValid)
                                          {
                                            SERIAL_PRINTLN("CONGRATULATIONS! Valid block found");
                                            // Commit the block record before announcing it. Never
                                            // use demo data or a periodic stats write for this record.
                                            valids = block_record::accept(itt->second->height,
                                                itt->second->ntime, blockSubsidySats(itt->second->height),
                                                itt->second->hash, valids + 1);
                                            diagnostics::event(diagnostics::Event::RealBlock);
                                            onBlockFound();
                                          }
                                          s_submition_map.erase(itt);
                                        }
                                      }
                                      break;
          case STRATUM_PARSE_ERROR:   {
                                        unsigned long id = parse_extract_id(line);
                                        auto itt = s_submition_map.find(id);
                                        if (itt != s_submition_map.end())
                                        {
                                          diagnostics::event(diagnostics::Event::Rejected);
                                          SERIAL_PRINTF("Refuse submition %d\n", id);
                                          onShareRejected();
                                          s_submition_map.erase(itt);
                                        }
                                      }
                                      break;
          default:                    SERIAL_PRINTLN("  Parsed JSON: unknown"); break;

      }
    }

    // Re-send an unacknowledged BLOCK on the live connection until the pool
    // responds. Same id/frame, so an eventual ACK still matches the submission
    // map. Stops automatically once the pool answers (the STRATUM_SUCCESS/reject
    // handler erases the map entry) or a job reset clears the map, or the
    // connection drops (a solved block can't be re-sent on a new session).
    if (s_blockResend.active) {
      if (!client.connected() ||
          s_submition_map.find(s_blockResend.id) == s_submition_map.end()) {
        s_blockResend.active = false;
      } else if (millis() - s_blockResend.lastMs >= BLOCK_RESEND_MS &&
                 s_blockResend.count < BLOCK_RESEND_MAX) {
        s_blockResend.count++;
        SERIAL_PRINTF("[WORKER] Re-sending unacknowledged BLOCK (attempt %d/%d)\n",
                      s_blockResend.count, BLOCK_RESEND_MAX);
        client.print(s_blockResend.payload);
        s_blockResend.lastMs = millis();
      }
    }

    std::list<std::shared_ptr<JobResult>> job_result_list;
    #ifdef I2C_SLAVE
    if (i2c_slave_vector.empty() || job_pool == 0xFFFFFFFF)
    {
      vTaskDelay(50 / portTICK_PERIOD_MS); //Small delay
    } else
    {
      uint32_t time_start = millis();
      i2c_hit_slaves(i2c_slave_vector);
      vTaskDelay(5 / portTICK_PERIOD_MS);
      uint32_t nonces_done = 0;
      std::vector<uint32_t> nonce_vector = i2c_harvest_slaves(i2c_slave_vector, job_pool & 0xFF, nonces_done);
      { std::lock_guard<std::mutex> statsLock(statsValueMutex); hashes += nonces_done; }
      for (size_t n = 0; n < nonce_vector.size(); ++n)
      {
        std::shared_ptr<JobResult> result = std::make_shared<JobResult>();
        ((uint32_t*)(mMiner.bytearray_blockheader+64+12))[0] = nonce_vector[n];
        if (nerd_sha256d_baked(diget_mid, mMiner.bytearray_blockheader+64, bake, result->hash))
        {
          result->id = job_pool;
          result->nonce = nonce_vector[n];
          result->nonce_count = 0;
          result->difficulty = diff_from_target(result->hash);
          job_result_list.push_back(result);
        }
      }
      uint32_t time_end = millis();
      //if (nonces_done > 16384)
        //SERIAL_PRINTF("Harvest slaves in %dms hashes=%d\n", time_end - time_start, nonces_done);
      if (time_end > time_start)
      {
        uint32_t elapsed = time_end - time_start;
        if (elapsed < 50)
          vTaskDelay((50 - elapsed) / portTICK_PERIOD_MS);
      } else
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
    #else
    vTaskDelay(50 / portTICK_PERIOD_MS); //Small delay
    #endif

    
    if (!device::batterySaverIdle() && session.ready() && job_pool != 0xFFFFFFFF)
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      job_result_list.insert(job_result_list.end(), s_job_result_list.begin(), s_job_result_list.end());
      s_job_result_list.clear();

#if 1
      if (!g_singleCoreMode && !g_thermalSingleCore) {
        while (s_job_request_list_sw.size() < 4)
        {
          JobPush( s_job_request_list_sw, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake);
          #ifdef RANDOM_NONCE
          nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
          #else
          nonce_pool += NONCE_PER_JOB_SW;
          #endif
        }
      }
#endif

      #ifdef HARDWARE_SHA265
      while (s_job_request_list_hw.size() < 4)
      {
        #if defined(CONFIG_IDF_TARGET_ESP32)
          JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, hw_midstate, bake);
        #else
          JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake);
        #endif
        #ifdef RANDOM_NONCE
        nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
        #else
        nonce_pool += NONCE_PER_JOB_HW;
        #endif
      }
      #endif
    }

    while (!job_result_list.empty())
    {
      std::shared_ptr<JobResult> res = job_result_list.front();
      job_result_list.pop_front();

      { std::lock_guard<std::mutex> statsLock(statsValueMutex); hashes += res->nonce_count; }
      if (res->difficulty > currentPoolDifficulty && job_pool == res->id && res->nonce != 0xFFFFFFFF)
      {
        if (!client.connected())
          break;

        // Determine up front whether this is a full BLOCK (meets the network
        // target), not just a pool share — so we can protect its submission.
        bool is32bit = (res->hash[29] == 0 && res->hash[28] == 0);
        bool isBlock = is32bit && checkValid(res->hash, mMiner.bytearray_target);

        unsigned long sumbit_id = 0;
        // Capture the exact frame only for a block, straight into the persistent
        // re-send buffer (no separate 1KB stack scratch on the worker task).
        bool wrote = tx_mining_submit(client, mWorker, mJob, res->nonce, sumbit_id,
                                      isBlock ? s_blockResend.payload : nullptr,
                                      isBlock ? sizeof(s_blockResend.payload) : 0);
        // A failed write means the socket is half-open. For a BLOCK, retry the
        // write a few times immediately on the same connection (covers a
        // transient short-write); the periodic re-send below covers the rest.
        if (isBlock && !wrote) {
          for (int r = 0; r < 3 && !wrote && client.connected(); r++) {
            vTaskDelay(150 / portTICK_PERIOD_MS);
            wrote = (client.print(s_blockResend.payload) == strlen(s_blockResend.payload));
          }
        }
        if (!wrote)
          diagnostics::event(diagnostics::Event::SubmitFailed);
          SERIAL_PRINTF("[WORKER] submit write %s\n", isBlock ? "FAILED for a BLOCK - will keep re-sending" : "failed (share lost)");

        SERIAL_PRINT("   - Current diff share: "); SERIAL_PRINTLN(res->difficulty,12);
        SERIAL_PRINT("   - Current pool diff : "); SERIAL_PRINTLN(currentPoolDifficulty,12);
        SERIAL_PRINT("   - TX SHARE: ");
        for (size_t i = 0; i < 32; i++)
            SERIAL_PRINTF("%02x", res->hash[i]);
        SERIAL_PRINTLN("");
        mLastTXtoPool = millis();

        std::shared_ptr<Submition> submition = std::make_shared<Submition>();
        submition->diff = res->difficulty;
        submition->is32bit = is32bit;
        submition->isValid = isBlock;

        // Snapshot block identity for the celebration screen. res->hash is the
        // header double-SHA in little-endian (natural SHA order); block
        // explorers show it big-endian, so reverse it for hash/URL display.
        submition->height = parseBlockHeightFromCoinb1(mJob.coinb1);
        submition->ntime  = (uint32_t)strtoul(mJob.ntime.c_str(), NULL, 16);
        for (int i = 0; i < 32; i++)
          snprintf(submition->hash + i * 2, 3, "%02x", res->hash[31 - i]);
        submition->hash[64] = '\0';

        s_submition_map.insert(std::make_pair(sumbit_id, submition));
        // Keep the map bounded, but never evict an unacknowledged BLOCK. Evict the
        // oldest ordinary share instead (there are always plenty when size>32, so
        // the map stays bounded and a pending block is protected).
        if (s_submition_map.size() > 32) {
          for (auto it = s_submition_map.begin(); it != s_submition_map.end(); ++it) {
            if (!it->second->isValid) { s_submition_map.erase(it); break; }
          }
        }

        // Arm re-sending for a block: push the same frame (already captured into
        // s_blockResend.payload by tx_mining_submit above) until the pool ACKs it.
        if (isBlock) {
          s_blockResend.active = true;
          s_blockResend.id     = sumbit_id;
          s_blockResend.lastMs = millis();
          s_blockResend.count  = 0;
        }
      }
    }

  }
}

//////////////////THREAD CALLS///////////////////

void minerWorkerSw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  SERIAL_PRINTF("[MINER] %d Started minerWorkerSw Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;
  uint8_t hash[32];
  uint32_t wdt_counter = 0;
  while (1)
  {
    if(device::batterySaverIdle()){
      job.reset();result.reset();esp_task_wdt_reset();
      device::lightSleepCheckpoint(miner_id==0 ? device::SleepRole::Miner0 : device::SleepRole::Miner1);
      vTaskDelay(pdMS_TO_TICKS(100));continue;
    }
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (result)
      {
        if (s_job_result_list.size() < 16)
          s_job_result_list.push_back(result);
        result.reset();
      }
      if (!s_job_request_list_sw.empty())
      {
        job = s_job_request_list_sw.front();
        s_job_request_list_sw.pop_front();
      } else
        job.reset();
    }
    if (job)
    {
      result = std::make_shared<JobResult>();
      result->difficulty = job->difficulty;
      result->nonce = 0xFFFFFFFF;
      result->id = job->id;
      result->nonce_count = job->nonce_count;
      uint32_t job_in_work = job->id;
      for (uint32_t n = 0; n < job->nonce_count; ++n)
      {
        ((uint32_t*)(job->sha_buffer+64+12))[0] = job->nonce_start+n;
        if (nerd_sha256d_baked(job->midstate, job->sha_buffer+64, job->bake, hash))
        {
          double diff_hash = diff_from_target(hash);
          if (diff_hash > result->difficulty)
          {
            result->difficulty = diff_hash;
            result->nonce = job->nonce_start+n;
            memcpy(result->hash, hash, 32);
          }
        }

        if ( (uint16_t)(n & 0xFF) == 0 &&(device::batterySaverIdle() || s_working_current_job_id != job_in_work))
        {
          result->nonce_count = n+1;
          break;
        }
      }
    } else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= 8)
    {
      wdt_counter = 0;
      esp_task_wdt_reset();
    }
  }
}

#ifdef HARDWARE_SHA265

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)

static inline void nerd_sha_ll_fill_text_block_sha256(const void *input_text, uint32_t nonce)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    REG_WRITE(&reg_addr_buf[0], data_words[0]);
    REG_WRITE(&reg_addr_buf[1], data_words[1]);
    REG_WRITE(&reg_addr_buf[2], data_words[2]);
#if 0
    REG_WRITE(&reg_addr_buf[3], nonce);
    //REG_WRITE(&reg_addr_buf[3], data_words[3]);    
    REG_WRITE(&reg_addr_buf[4], data_words[4]);
    REG_WRITE(&reg_addr_buf[5], data_words[5]);
    REG_WRITE(&reg_addr_buf[6], data_words[6]);
    REG_WRITE(&reg_addr_buf[7], data_words[7]);
    REG_WRITE(&reg_addr_buf[8], data_words[8]);
    REG_WRITE(&reg_addr_buf[9], data_words[9]);
    REG_WRITE(&reg_addr_buf[10], data_words[10]);
    REG_WRITE(&reg_addr_buf[11], data_words[11]);
    REG_WRITE(&reg_addr_buf[12], data_words[12]);
    REG_WRITE(&reg_addr_buf[13], data_words[13]);
    REG_WRITE(&reg_addr_buf[14], data_words[14]);
    REG_WRITE(&reg_addr_buf[15], data_words[15]);
#else
    REG_WRITE(&reg_addr_buf[3], nonce);
    REG_WRITE(&reg_addr_buf[4], 0x00000080);
    REG_WRITE(&reg_addr_buf[5], 0x00000000);
    REG_WRITE(&reg_addr_buf[6], 0x00000000);
    REG_WRITE(&reg_addr_buf[7], 0x00000000);
    REG_WRITE(&reg_addr_buf[8], 0x00000000);
    REG_WRITE(&reg_addr_buf[9], 0x00000000);
    REG_WRITE(&reg_addr_buf[10], 0x00000000);
    REG_WRITE(&reg_addr_buf[11], 0x00000000);
    REG_WRITE(&reg_addr_buf[12], 0x00000000);
    REG_WRITE(&reg_addr_buf[13], 0x00000000);
    REG_WRITE(&reg_addr_buf[14], 0x00000000);
    REG_WRITE(&reg_addr_buf[15], 0x80020000);
#endif
}

static inline void nerd_sha_ll_fill_text_block_sha256_inter()
{
  uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

  DPORT_INTERRUPT_DISABLE();
  REG_WRITE(&reg_addr_buf[0], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4));
  REG_WRITE(&reg_addr_buf[1], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4));
  REG_WRITE(&reg_addr_buf[2], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4));
  REG_WRITE(&reg_addr_buf[3], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4));
  REG_WRITE(&reg_addr_buf[4], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4));
  REG_WRITE(&reg_addr_buf[5], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4));
  REG_WRITE(&reg_addr_buf[6], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4));
  REG_WRITE(&reg_addr_buf[7], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4));
  DPORT_INTERRUPT_RESTORE();

  REG_WRITE(&reg_addr_buf[8], 0x00000080);
  REG_WRITE(&reg_addr_buf[9], 0x00000000);
  REG_WRITE(&reg_addr_buf[10], 0x00000000);
  REG_WRITE(&reg_addr_buf[11], 0x00000000);
  REG_WRITE(&reg_addr_buf[12], 0x00000000);
  REG_WRITE(&reg_addr_buf[13], 0x00000000);
  REG_WRITE(&reg_addr_buf[14], 0x00000000);
  REG_WRITE(&reg_addr_buf[15], 0x00010000);
}

static inline void nerd_sha_ll_read_digest(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);  
  ((uint32_t*)ptr)[7] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  DPORT_INTERRUPT_RESTORE();
}


static inline bool nerd_sha_ll_read_digest_if(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  uint32_t last = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  #if 1
  if ( (uint16_t)(last >> 16) != 0)
  {
    DPORT_INTERRUPT_RESTORE();
    return false;
  }
  #endif

  ((uint32_t*)ptr)[7] = last;
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);  
  DPORT_INTERRUPT_RESTORE();
  return true;
}

static inline void nerd_sha_ll_write_digest(void *digest_state)
{
    uint32_t *digest_state_words = (uint32_t *)digest_state;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_H_BASE);

    REG_WRITE(&reg_addr_buf[0], digest_state_words[0]);
    REG_WRITE(&reg_addr_buf[1], digest_state_words[1]);
    REG_WRITE(&reg_addr_buf[2], digest_state_words[2]);
    REG_WRITE(&reg_addr_buf[3], digest_state_words[3]);
    REG_WRITE(&reg_addr_buf[4], digest_state_words[4]);
    REG_WRITE(&reg_addr_buf[5], digest_state_words[5]);
    REG_WRITE(&reg_addr_buf[6], digest_state_words[6]);
    REG_WRITE(&reg_addr_buf[7], digest_state_words[7]);
}

static inline void nerd_sha_hal_wait_idle()
{
    while (REG_READ(SHA_BUSY_REG))
    {}
}

//#define VALIDATION
void minerWorkerHw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  SERIAL_PRINTF("[MINER] %d Started minerWorkerHw Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;
  uint8_t interResult[64];
  uint8_t hash[32];
  uint8_t digest_mid[32];
  uint8_t sha_buffer[64];
  uint32_t wdt_counter = 0;

#ifdef VALIDATION
  uint8_t doubleHash[32];
  uint32_t diget_mid[8];
  uint32_t bake[16];
#endif

  while (1)
  {
    if(device::batterySaverIdle()){
      job.reset();result.reset();esp_task_wdt_reset();
      device::lightSleepCheckpoint(miner_id==0 ? device::SleepRole::Miner0 : device::SleepRole::Miner1);
      vTaskDelay(pdMS_TO_TICKS(100));continue;
    }
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (result)
      {
        if (s_job_result_list.size() < 16)
          s_job_result_list.push_back(result);
        result.reset();
      }
      if (!s_job_request_list_hw.empty())
      {
        job = s_job_request_list_hw.front();
        s_job_request_list_hw.pop_front();
      } else
        job.reset();
    }
    if (job)
    {
      result = std::make_shared<JobResult>();
      result->id = job->id;
      result->nonce = 0xFFFFFFFF;
      result->nonce_count = job->nonce_count;
      result->difficulty = job->difficulty;
      uint32_t job_in_work = job->id;
      memcpy(digest_mid, job->midstate, sizeof(digest_mid));
      memcpy(sha_buffer, job->sha_buffer+64, sizeof(sha_buffer));
#ifdef VALIDATION
      nerd_mids(diget_mid, job->sha_buffer);
      nerd_sha256_bake(diget_mid, job->sha_buffer+64, bake);
#endif

      esp_sha_acquire_hardware();
      REG_WRITE(SHA_MODE_REG, SHA2_256);
      // Clamp nonce range to prevent uint32_t wrap-around when nonce_start is near 0xFFFFFFFF
      uint32_t nend = job->nonce_start + job->nonce_count;
      if (nend < job->nonce_start) nend = 0xFFFFFFFF;  // overflow — clamp to max
      for (uint32_t n = job->nonce_start; n < nend; ++n)
      {
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_write_digest(digest_mid);
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256(sha_buffer, n);
        //sha_ll_continue_block(SHA2_256);
        REG_WRITE(SHA_CONTINUE_REG, 1);
        
        sha_ll_load(SHA2_256);
        nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256_inter();
        //sha_ll_start_block(SHA2_256);
        REG_WRITE(SHA_START_REG, 1);
        sha_ll_load(SHA2_256);
        nerd_sha_hal_wait_idle();
        if (nerd_sha_ll_read_digest_if(hash))
        {
          //SERIAL_PRINTF("Hw 16bit Share, nonce=0x%X\n", n);
#ifdef VALIDATION
          //Validation
          ((uint32_t*)(job->sha_buffer+64+12))[0] = n;
          nerd_sha256d_baked(diget_mid, job->sha_buffer+64, bake, doubleHash);
          for (int i = 0; i < 32; ++i)
          {
            if (hash[i] != doubleHash[i])
            {
              diagnostics::event(diagnostics::Event::HashError);
              SERIAL_PRINTLN("***HW sha256 esp32s3 bug detected***");
              break;
            }
          }
#endif
          //~5 per second
          double diff_hash = diff_from_target(hash);
          if (diff_hash > result->difficulty)
          {
            if (isSha256Valid(hash))
            {
              result->difficulty = diff_hash;
              result->nonce = n;
              memcpy(result->hash, hash, sizeof(hash));
            }
          }
        }
        if ((uint8_t)(n & 0xFF) == 0) {
          // The device controller can pause hashing to cool the watch.
          if (g_thermalPause) {
            esp_sha_release_hardware();
            // Feed the watchdog during a deliberate thermal pause.
            while (g_thermalPause && !device::batterySaverIdle()) {
              vTaskDelay(pdMS_TO_TICKS(10));
              esp_task_wdt_reset();
            }
            esp_sha_acquire_hardware();
            REG_WRITE(SHA_MODE_REG, SHA2_256);
          }
          if (device::batterySaverIdle() || s_working_current_job_id != job_in_work) {
            result->nonce_count = n-job->nonce_start+1;
            break;
          }
        }
      }
      esp_sha_release_hardware();

      // Duty cycle: pause between batches to save battery (profile) and/or shed
      // heat (thermal governor). Take the stricter of the two.
      {
        uint32_t duty = g_miningDutyCycleMs;
        if (g_thermalDutyFloorMs > duty) duty = g_thermalDutyFloorMs;
        if (duty > 0) {
          vTaskDelay(pdMS_TO_TICKS(duty));
        }
      }
    } else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= 8)
    {
      wdt_counter = 0;
      esp_task_wdt_reset();
    }
  }
}

#endif  //#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)

#if defined(CONFIG_IDF_TARGET_ESP32)

static inline bool nerd_sha_ll_read_digest_swap_if(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  uint32_t fin = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
  if ( (uint32_t)(fin & 0xFFFF) != 0)
  {
    DPORT_INTERRUPT_RESTORE();
    return false;
  }
  ((uint32_t*)ptr)[7] = __builtin_bswap32(fin);
  ((uint32_t*)ptr)[0] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4));
  ((uint32_t*)ptr)[1] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4));
  ((uint32_t*)ptr)[2] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4));
  ((uint32_t*)ptr)[3] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4));
  ((uint32_t*)ptr)[4] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4));
  ((uint32_t*)ptr)[5] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4));
  ((uint32_t*)ptr)[6] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4));
  DPORT_INTERRUPT_RESTORE();
  return true;
}

static inline void nerd_sha_ll_read_digest(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4);
  ((uint32_t*)ptr)[7] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
  DPORT_INTERRUPT_RESTORE();
}

static inline void nerd_sha_hal_wait_idle()
{
    while (DPORT_REG_READ(SHA_256_BUSY_REG))
    {}
}

static inline void nerd_sha_ll_fill_text_block_sha256(const void *input_text)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = data_words[3];
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
    reg_addr_buf[8]  = data_words[8];
    reg_addr_buf[9]  = data_words[9];
    reg_addr_buf[10] = data_words[10];
    reg_addr_buf[11] = data_words[11];
    reg_addr_buf[12] = data_words[12];
    reg_addr_buf[13] = data_words[13];
    reg_addr_buf[14] = data_words[14];
    reg_addr_buf[15] = data_words[15];
}

static inline void nerd_sha_ll_fill_text_block_sha256_upper(const void *input_text, uint32_t nonce)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = __builtin_bswap32(nonce);
#if 1
    reg_addr_buf[4]  = 0x80000000;
    reg_addr_buf[5]  = 0x00000000;
    reg_addr_buf[6]  = 0x00000000;
    reg_addr_buf[7]  = 0x00000000;
    reg_addr_buf[8]  = 0x00000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000280;
#else
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
    reg_addr_buf[8]  = data_words[8];
    reg_addr_buf[9]  = data_words[9];
    reg_addr_buf[10] = data_words[10];
    reg_addr_buf[11] = data_words[11];
    reg_addr_buf[12] = data_words[12];
    reg_addr_buf[13] = data_words[13];
    reg_addr_buf[14] = data_words[14];
    reg_addr_buf[15] = data_words[15];
#endif
}

static inline void nerd_sha_ll_fill_text_block_sha256_double()
{
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

#if 0
    //No change
    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = data_words[3];
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
#endif
    reg_addr_buf[8]  = 0x80000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000100;
}

void minerWorkerHw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  SERIAL_PRINTF("[MINER] %d Started minerWorkerHwEsp32D Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;
  uint8_t hash[32];
  uint8_t sha_buffer[128];

  while (1)
  {
    if(device::batterySaverIdle()){
      job.reset();result.reset();esp_task_wdt_reset();
      device::lightSleepCheckpoint(miner_id==0 ? device::SleepRole::Miner0 : device::SleepRole::Miner1);
      vTaskDelay(pdMS_TO_TICKS(100));continue;
    }
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (result)
      {
        if (s_job_result_list.size() < 16)
          s_job_result_list.push_back(result);
        result.reset();
      }
      if (!s_job_request_list_hw.empty())
      {
        job = s_job_request_list_hw.front();
        s_job_request_list_hw.pop_front();
      } else
        job.reset();
    }
    if (job)
    {
      result = std::make_shared<JobResult>();
      result->id = job->id;
      result->nonce = 0xFFFFFFFF;
      result->nonce_count = job->nonce_count;
      result->difficulty = job->difficulty;
      uint32_t job_in_work = job->id;
      memcpy(sha_buffer, job->sha_buffer, 80);

      esp_sha_lock_engine(SHA2_256);
      for (uint32_t n = 0; n < job->nonce_count; ++n)
      {
        //((uint32_t*)(sha_buffer+64+12))[0] = __builtin_bswap32(job->nonce_start+n);

        //sha_hal_hash_block(SHA2_256, s_test_buffer, 64/4, true);
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256(sha_buffer);
        sha_ll_start_block(SHA2_256);

        //sha_hal_hash_block(SHA2_256, s_test_buffer+64, 64/4, false);
        nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256_upper(sha_buffer+64, job->nonce_start+n);
        sha_ll_continue_block(SHA2_256);

        nerd_sha_hal_wait_idle();
        sha_ll_load(SHA2_256);

        //sha_hal_hash_block(SHA2_256, interResult, 64/4, true);
        nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256_double();
        sha_ll_start_block(SHA2_256);

        nerd_sha_hal_wait_idle();
        sha_ll_load(SHA2_256);
        if (nerd_sha_ll_read_digest_swap_if(hash))
        {
          //~5 per second
          double diff_hash = diff_from_target(hash);
          if (diff_hash > result->difficulty)
          {
            if (isSha256Valid(hash))
            {
              result->difficulty = diff_hash;
              result->nonce = job->nonce_start+n;
              memcpy(result->hash, hash, sizeof(hash));
            }
          }
        }
        if (
             (uint8_t)(n & 0xFF) == 0 &&
             s_working_current_job_id != job_in_work)
        {
          result->nonce_count = n+1;
          break;
        }
      }
      esp_sha_unlock_engine(SHA2_256);
    } else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    esp_task_wdt_reset();
  }
}

#endif  //CONFIG_IDF_TARGET_ESP32

#endif  //HARDWARE_SHA265


#define DELAY 150       // Battery optimized - 150ms loop instead of 100ms
#define REDRAW_EVERY 10

// A single checksummed NVS blob prevents a reset between independent counter
// writes from invalidating all statistics. Legacy values are read only for migration.
static bool openStats() {
  if(statsReady)return true;
  if(nvs_flash_init()!=ESP_OK)return false; // never erase other saved data
  statsReady=nvs_open("state",NVS_READWRITE,&stat_handle)==ESP_OK;
  return statsReady;
}
static bool loadLegacyStats(mining_stats::Snapshot& value) {
  mining_stats::Snapshot s;
  uint32_t storedCrc=0;size_t size=sizeof(s.best);
  if(nvs_get_blob(stat_handle,"best_diff",&s.best,&size)!=ESP_OK || size!=sizeof(s.best) ||
     nvs_get_u32(stat_handle,"Mhashes",&s.mega)!=ESP_OK ||
     nvs_get_u32(stat_handle,"shares",&s.shares)!=ESP_OK ||
     nvs_get_u32(stat_handle,"valids",&s.blocks)!=ESP_OK ||
     nvs_get_u32(stat_handle,"templates",&s.jobs)!=ESP_OK ||
     nvs_get_u64(stat_handle,"upTime",&s.uptime)!=ESP_OK ||
     nvs_get_u32(stat_handle,"crc32",&storedCrc)!=ESP_OK)return false;
  uint32_t crc=crc32_reset();
  crc=crc32_add(crc,&s.best,sizeof(s.best));crc=crc32_add(crc,&s.mega,sizeof(s.mega));
  crc=crc32_add(crc,&s.shares,sizeof(s.shares));crc=crc32_add(crc,&s.blocks,sizeof(s.blocks));
  crc=crc32_add(crc,&s.jobs,sizeof(s.jobs));crc=crc32_add(crc,&s.uptime,sizeof(s.uptime));
  if(crc32_finish(crc)!=storedCrc || !std::isfinite(s.best) || s.best<0)return false;
  value=s;return true;
}
void restoreStat() {
  std::lock_guard<std::mutex> io(statsIoMutex);
  mining_stats::Snapshot s;
  if(openStats()) {
    uint8_t bytes[mining_stats::Bytes];size_t size=sizeof(bytes);
    if(nvs_get_blob(stat_handle,"snapshot_v1",bytes,&size)!=ESP_OK ||
       !mining_stats::decode(bytes,size,s)) loadLegacyStats(s);
  }
  std::lock_guard<std::mutex> values(statsValueMutex);
  best_diff=s.best;Mhashes=s.mega;hashes=s.remainder;shares=s.shares;
  valids=s.blocks;templates=s.jobs;upTime=0; // uptime is always per boot
}
bool saveStat() {
  std::lock_guard<std::mutex> io(statsIoMutex);
  statsLastAttempt=millis();
  mining_stats::Snapshot s;
  {
    std::lock_guard<std::mutex> values(statsValueMutex);
    s.best=best_diff;s.mega=Mhashes;s.remainder=hashes;
    s.shares=shares;s.blocks=valids;s.jobs=templates;s.uptime=upTime;
  }
  uint8_t bytes[mining_stats::Bytes];mining_stats::encode(s,bytes);
  const bool ok=statsReady.load() && nvs_set_blob(stat_handle,"snapshot_v1",bytes,sizeof(bytes))==ESP_OK &&
      nvs_commit(stat_handle)==ESP_OK;
  statsSaveFailed=!ok;
  return ok;
}
void checkpointMiningStats() {
  // Charging-only boot has not restored counters: never overwrite saved totals
  // with zeroes in that mode. Normal monitor startup owns restore/migration.
  if(statsReady)saveStat();
}
void resetStat() {
  {
    std::lock_guard<std::mutex> values(statsValueMutex);
    templates=hashes=Mhashes=totalKHashes=elapsedKHs=upTime=shares=valids=0;
    best_diff=0.0;
  }
  saveStat();
}

void runMonitor(void *name)
{

  SERIAL_PRINTLN("[MONITOR] started");
  restoreStat();
  block_record::initialize();
  const auto savedBlock=block_record::snapshot();
  if(savedBlock.valid() && savedBlock.count>valids)valids=savedBlock.count;

  unsigned long mLastCheck = 0;

  // Initialize first screen with mutex protection
  if (lvgl_mutex != NULL && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    resetToFirstScreen();
    xSemaphoreGive(lvgl_mutex);
  } else {
    resetToFirstScreen();  // Fallback if mutex not ready yet
  }

  unsigned long frame = 0;

  statsLastAttempt=millis();

  totalKHashes = (Mhashes * 1000) + hashes / 1000;
  uint32_t last_update_millis = millis();
  uint32_t uptime_frac = 0;

  while (1)
  {
    device::lightSleepCheckpoint(device::SleepRole::Monitor);
    block_record::service();
    uint32_t now_millis = millis();
    if (now_millis < last_update_millis)
      now_millis = last_update_millis;
    
    uint32_t mElapsed = now_millis - mLastCheck;
    if (mElapsed >= 1000)
    { 
      mLastCheck = now_millis;
      last_update_millis = now_millis;
      unsigned long currentKHashes = (Mhashes * 1000) + hashes / 1000;
      // Mhashes/hashes are updated by the stratum worker without a lock; a read
      // landing mid-update can momentarily yield currentKHashes < totalKHashes,
      // which would underflow the unsigned subtraction into a ~4-billion KH/s
      // spike on screen for one sample. Clamp instead.
      elapsedKHs = (currentKHashes > totalKHashes) ? (currentKHashes - totalKHashes) : 0;
      totalKHashes = currentKHashes;
      updateHashrateSample(elapsedKHs,mElapsed);
      diagnostics::miningRate(elapsedKHs,mElapsed);


      {
      std::lock_guard<std::mutex> statsLock(statsValueMutex);
      uptime_frac += mElapsed;
      while (uptime_frac >= 1000)
      {
        uptime_frac -= 1000;
        upTime ++;
      }
      }

      // LVGL display update with mutex protection - skip if screen is off
      if (isScreenActive()) {
        if (lvgl_mutex != NULL && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          drawCurrentScreen(mElapsed);
          xSemaphoreGive(lvgl_mutex);
        }
      }

      // Monitor state when hashrate is 0.0
      if (elapsedKHs == 0)
      {
        SERIAL_PRINTF(">>> [i] Miner: newJob>%s / inRun>%s) - Client: connected>%s / subscribed>%s / wificonnected>%s\n",
            "true",//(1) ? "true" : "false",
            isMinerSuscribed ? "true" : "false",
            client.connected() ? "true" : "false", isMinerSuscribed ? "true" : "false", WiFi.status() == WL_CONNECTED ? "true" : "false");
      }

      #ifdef DEBUG_MEMORY
      SERIAL_PRINTF("### [Total Heap / Free heap / Min free heap]: %d / %d / %d \n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
      SERIAL_PRINTF("### Max stack usage: %d\n", uxTaskGetStackHighWaterMark(NULL));
      #endif

      if(uint32_t(millis()-statsLastAttempt)>=(statsSaveFailed ? 30000u : STATS_SAVE_MS))
        saveStat();
    }
    
    // Poll real finds even with the panel asleep. The driver itself skips
    // dark-screen rendering, but must be able to wake for a new block.
    {
      if (lvgl_mutex != NULL && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        animateCurrentScreen(frame);
        xSemaphoreGive(lvgl_mutex);
      }
    }
    
    doLedStuff(frame);

    // Sleep in short slices so a swipe can wake us early for immediate redraw
    for (int _s = 0; _s < DELAY / 10; _s++) {
      vTaskDelay(pdMS_TO_TICKS(10));
      if (needsImmediateRedraw) break;
    }
    if (needsImmediateRedraw) {
      if (isScreenActive()) {
        if (lvgl_mutex != NULL && xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
          needsImmediateRedraw.exchange(false);
          animateCurrentScreen(frame);
          xSemaphoreGive(lvgl_mutex);
        }
      }
    }
    frame++;
  }
}