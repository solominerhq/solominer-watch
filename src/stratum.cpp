#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ctype.h>   // isxdigit — used by the mining.notify field validators
#include "stratum.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "lwip/sockets.h"
#include "utils.h"
#include "version.h"
#include "seriallog.h"



StaticJsonDocument<BUFFER_JSON_DOC> doc;
unsigned long id = 1;

//Get next JSON RPC Id
unsigned long getNextId(unsigned long id) {
    if (id == ULONG_MAX) {
      id = 1;
      return id;
    }
    return ++id;
}

//Verify Payload doesn't has zero lenght
bool verifyPayload (String* line){
  if(line->length() == 0) return false;
  line->trim();
  if(line->isEmpty()) return false;
  return true;

}

// Coinbase halves are pool-controlled. The assembled coinbase buffer in
// utils.cpp is 2048 bytes: cap each half so coinb1 + extranonce1(<=16) +
// extranonce2(<=16) + coinb2 always fits with room to spare.
#define MAX_COINBASE_PART_LEN 960

// True if v is a hex string of exactly len chars. Network fields are copied
// into fixed-size buffers downstream — length is part of validity.
static bool isHexLen(JsonVariantConst v, size_t len) {
    const char* s = (const char*) v;
    if (!s || strlen(s) != len) return false;
    for (size_t i = 0; i < len; i++) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

// True if v is an even-length hex string no longer than maxLen (may be empty).
static bool isHexEven(JsonVariantConst v, size_t maxLen) {
    const char* s = (const char*) v;
    if (!s) return false;
    size_t n = strlen(s);
    if (n > maxLen || (n & 1)) return false;
    for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

bool checkError(const StaticJsonDocument<BUFFER_JSON_DOC>& doc) {
  
  if (!doc.containsKey("error")) return false;
  
  if (doc["error"].size() == 0) return false;

  SERIAL_PRINTF("ERROR: %d | reason: %s \n", (const int) doc["error"][0], (const char*) doc["error"][1]);

  return true;  
}


// STEP 1: Pool server connection (SUBSCRIBE)
    // Docs: 
    // - https://cs.braiins.com/stratum-v1/docs
    // - https://github.com/aeternity/protocol/blob/master/STRATUM.md#mining-subscribe
bool tx_mining_subscribe(WiFiClient& client, mining_subscribe& mSubscribe)
{
    char payload[BUFFER] = {0};
    
    // Subscribe
    id = 1; //Initialize id messages
    snprintf(payload, sizeof(payload), "{\"id\": %u, \"method\": \"mining.subscribe\", \"params\": [\"SoloMiner/%s\"]}\n", id, CURRENT_VERSION);
    
    SERIAL_PRINTF("[WORKER] ==> Mining subscribe\n");
    SERIAL_PRINT("  Sending  : "); SERIAL_PRINTLN(payload);
    client.print(payload);
    
    vTaskDelay(200 / portTICK_PERIOD_MS); //Small delay
    
    String line = client.readStringUntil('\n');
    if(!parse_mining_subscribe(line, mSubscribe)) return false;

  
    SERIAL_PRINT("    sub_details: "); SERIAL_PRINTLN(mSubscribe.sub_details);
    SERIAL_PRINT("    extranonce1: "); SERIAL_PRINTLN(mSubscribe.extranonce1);
    SERIAL_PRINT("    extranonce2_size: "); SERIAL_PRINTLN(mSubscribe.extranonce2_size);

    if((mSubscribe.extranonce1.length() == 0) ) { 
        SERIAL_PRINTF("[WORKER] >>>>>>>>> Work aborted\n");
        SERIAL_PRINTF("extranonce1 length: %u \n", mSubscribe.extranonce1.length());
        doc.clear();
        doc.garbageCollect();
        return false; 
    }
    return true;
}

bool parse_mining_subscribe(String line, mining_subscribe& mSubscribe)
{
    if(!verifyPayload(&line)) return false;
    SERIAL_PRINT("  Receiving: "); SERIAL_PRINTLN(line);
   
    DeserializationError error = deserializeJson(doc, line);

    if (error || checkError(doc)) return false;
    if (!doc.containsKey("result")) return false;

    // Defensive structure check. A valid subscribe result is a 3-element array:
    //   [ [[method,subid],...], extranonce1, extranonce2_size ]
    // A pool that REJECTS the subscribe instead returns result as a string,
    // null, false, or a short array (e.g. {"result":"Only allowed user agents
    // may subscribe"}). Indexing result[0][0][1] on a non-array dereferences
    // invalid memory → LoadProhibited crash/reboot. Validate before touching it.
    JsonVariant result = doc["result"];
    if (!result.is<JsonArray>() ||
        !result[0].is<JsonArray>() ||
        !result[0][0].is<JsonArray>() ||
        !result[1].is<const char*>()) {
        SERIAL_PRINTLN("[WORKER] Subscribe rejected or malformed - aborting connection");
        return false;
    }

    mSubscribe.sub_details = String((const char*) doc["result"][0][0][1]);
    mSubscribe.extranonce1 = String((const char*) doc["result"][1]);
    mSubscribe.extranonce2_size = doc["result"][2];

    return true;
}

mining_subscribe init_mining_subscribe(void)
{
    mining_subscribe new_mSub;

    new_mSub.extranonce1 = "";
    new_mSub.extranonce2 = "";
    new_mSub.extranonce2_size = 0;
    new_mSub.sub_details = "";


    return new_mSub;
}

// STEP 2: Pool server auth (authorize)
bool tx_mining_auth(WiFiClient& client, const char *user, const char *pass, unsigned long& requestId)
{
    StaticJsonDocument<512> request;
    id = getNextId(id);
    requestId = id;
    request["id"] = id;
    request["method"] = "mining.authorize";
    JsonArray params = request.createNestedArray("params");
    params.add(user);
    params.add(pass);
    // JSON serialization handles quotes/backslashes in pool passwords safely.
    char payload[BUFFER];
    if (request.overflowed() || measureJson(request) + 2 > sizeof(payload)) return false;
    size_t n = serializeJson(request, payload, sizeof(payload));
    payload[n++] = '\n';
    return client.write(reinterpret_cast<const uint8_t*>(payload), n) == n;
}

stratum_method parse_mining_method(String line)
{
    if(!verifyPayload(&line)) return STRATUM_PARSE_ERROR;
    SERIAL_PRINT("  Receiving: "); SERIAL_PRINTLN(line);
    
    DeserializationError error = deserializeJson(doc, line);

    if (error || checkError(doc)) return STRATUM_PARSE_ERROR;

    if (!doc.containsKey("method")) {
      // A false result is a rejection, even with error:null.
      if (doc["result"].is<bool>() && !doc["result"].as<bool>()) return STRATUM_PARSE_ERROR;
      if (doc["error"].isNull())
        return STRATUM_SUCCESS;
      else
        return STRATUM_UNKNOWN;
    }
    stratum_method result = STRATUM_UNKNOWN;

    if (strcmp("mining.notify", (const char*) doc["method"]) == 0) {
        result = MINING_NOTIFY;
    } else if (strcmp("mining.set_difficulty", (const char*) doc["method"]) == 0) {
        result = MINING_SET_DIFFICULTY;
    }

    return result;
}

bool parse_mining_notify(String line, mining_job& mJob)
{
    SERIAL_PRINTLN("    Parsing Method [MINING NOTIFY]");
    if(!verifyPayload(&line)) return false;
   
    DeserializationError error = deserializeJson(doc, line);

    if (error) return false;
    if (!doc.containsKey("params")) return false;

    // mining.notify params must be a 9-element array. A malformed/short array or
    // wrong element types from a buggy or hostile pool would otherwise be indexed
    // blindly (params[5..8]) and crash on a null deref. Validate the shape first.
    JsonVariant p = doc["params"];
    if (!p.is<JsonArray>() || p.size() < 9 ||
        !p[0].is<const char*>() || !p[1].is<const char*>() ||
        !p[2].is<const char*>() || !p[3].is<const char*>() ||
        !p[4].is<JsonArray>()  || !p[5].is<const char*>() ||
        !p[6].is<const char*>() || !p[7].is<const char*>()) {
        SERIAL_PRINTLN("[WORKER] Malformed mining.notify - ignoring");
        return false;
    }

    // Field-LENGTH validation. calculateMiningData() copies these into fixed
    // buffers (64-byte target, 80-byte header, 32-byte merkle elements) and a
    // shape-valid but wrong-length field from a hostile/buggy pool would turn
    // into an out-of-bounds read/write there. Exact lengths per the stratum
    // spec: prevhash 64 hex, version/nbits/ntime 8 hex, merkle elements 64 hex,
    // coinb1/coinb2 even-length hex (bounded so the assembled coinbase can
    // never overflow its buffer in utils.cpp).
    if (!isHexLen(p[1], 64) || !isHexLen(p[5], 8) ||
        !isHexLen(p[6], 8)  || !isHexLen(p[7], 8) ||
        !isHexEven(p[2], MAX_COINBASE_PART_LEN) ||
        !isHexEven(p[3], MAX_COINBASE_PART_LEN)) {
        SERIAL_PRINTLN("[WORKER] mining.notify field length/format invalid - ignoring");
        return false;
    }
    for (size_t k = 0; k < p[4].size(); k++) {
        if (!isHexLen(p[4][k], 64)) {
            SERIAL_PRINTLN("[WORKER] mining.notify merkle element invalid - ignoring");
            return false;
        }
    }

    mJob.job_id = String((const char*) doc["params"][0]);
    mJob.prev_block_hash = String((const char*) doc["params"][1]);
    mJob.coinb1 = String((const char*) doc["params"][2]);
    mJob.coinb2 = String((const char*) doc["params"][3]);
    mJob.merkle_branch = doc["params"][4];
    mJob.version = String((const char*) doc["params"][5]);
    mJob.nbits = String((const char*) doc["params"][6]);
    mJob.ntime = String((const char*) doc["params"][7]);
    mJob.clean_jobs = doc["params"][8]; //bool

    #ifdef DEBUG_MINING
    SERIAL_PRINT("    job_id: "); SERIAL_PRINTLN(mJob.job_id);
    SERIAL_PRINT("    prevhash: "); SERIAL_PRINTLN(mJob.prev_block_hash);
    SERIAL_PRINT("    coinb1: "); SERIAL_PRINTLN(mJob.coinb1);
    SERIAL_PRINT("    coinb2: "); SERIAL_PRINTLN(mJob.coinb2);
    SERIAL_PRINT("    merkle_branch size: "); SERIAL_PRINTLN(mJob.merkle_branch.size());
    SERIAL_PRINT("    version: "); SERIAL_PRINTLN(mJob.version);
    SERIAL_PRINT("    nbits: "); SERIAL_PRINTLN(mJob.nbits);
    SERIAL_PRINT("    ntime: "); SERIAL_PRINTLN(mJob.ntime);
    SERIAL_PRINT("    clean_jobs: "); SERIAL_PRINTLN(mJob.clean_jobs);
    #endif
    //Check if parameters where correctly received
    if (checkError(doc)) {
      SERIAL_PRINTF("[WORKER] >>>>>>>>> Work aborted\n");
      return false;
    }
    return true;
}


bool tx_mining_submit(WiFiClient& client, mining_subscribe mWorker, mining_job mJob, unsigned long nonce, unsigned long &submit_id, char* out_payload, size_t out_cap)
{
    char payload[BUFFER] = {0};

    // Submit
    id = getNextId(id);
    submit_id = id;
    // Nonce must be a fixed-width 8-hex-digit field; some stratum servers
    // reject shorter representations (looks like random share rejections).
    snprintf(payload, sizeof(payload), "{\"id\":%u,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%08lx\"]}\n",
        id,
        mWorker.wName,
        mJob.job_id.c_str(),
        mWorker.extranonce2.c_str(),
        mJob.ntime.c_str(),
        nonce
        );
    SERIAL_PRINT("  Sending  : "); SERIAL_PRINT(payload);

    // Hand the exact frame back to the caller so a solved block can be re-sent
    // on the same connection until the pool acknowledges it.
    if (out_payload && out_cap) {
        strncpy(out_payload, payload, out_cap - 1);
        out_payload[out_cap - 1] = '\0';
    }

    // Write-aware: report whether the whole frame actually went out. A half-open
    // socket writes 0 (or short), which is how a silently-lost submit is caught.
    size_t plen = strlen(payload);
    size_t wrote = client.print(payload);
    return wrote == plen;
}

bool parse_mining_set_difficulty(String line, double& difficulty)
{
    SERIAL_PRINTLN("    Parsing Method [SET DIFFICULTY]");
    if(!verifyPayload(&line)) return false;
   
    DeserializationError error = deserializeJson(doc, line);

    if (error) return false;
    if (!doc.containsKey("params")) return false;

    // params[0] must be a positive number. The pool may send it as an integer
    // (e.g. 100000) or a float (e.g. 0.00015), so don't type-check on double —
    // convert and validate the value. as<double>() yields 0 for null/non-numeric,
    // which the >0 check rejects (a malformed 0 would corrupt share comparisons).
    JsonVariant p = doc["params"];
    if (!p.is<JsonArray>() || p.size() < 1) {
        SERIAL_PRINTLN("[WORKER] Malformed set_difficulty - ignoring");
        return false;
    }
    double d = p[0].as<double>();
    if (!(d > 0)) return false;   // reject 0/NaN/negative/non-numeric

    SERIAL_PRINT("    difficulty: "); SERIAL_PRINTLN(d, 12);
    difficulty = d;

    return true;
}

bool tx_suggest_difficulty(WiFiClient& client, double difficulty)
{
    char payload[BUFFER] = {0};

    id = getNextId(id);
    snprintf(payload, sizeof(payload), "{\"id\":%d,\"method\":\"mining.suggest_difficulty\",\"params\":[%.10g]}\n", id, difficulty);
    
    SERIAL_PRINT("  Sending  : "); SERIAL_PRINT(payload);
    // Write-aware: true only if the whole keepalive frame went out. A half-open
    // socket short-writes, which lets the caller detect a dead pool connection.
    size_t plen = strlen(payload);
    return client.print(payload) == plen;
}


unsigned long parse_extract_id(const String &line)
{
    DeserializationError error = deserializeJson(doc, line);
    if (error)
        return 0;
    
    if (!doc.containsKey("id"))
        return 0;

    unsigned long id = doc["id"];

    return id;
}