#pragma once
#include <atomic>
#include <stdint.h>

// Pool connection phase — surfaced on the clock screen during the gap between
// WiFi connecting and mining, so the user can see what's actually happening
// instead of just the pool name (a slow or failing pool connect is otherwise a
// silent multi-minute wait). Set by the Stratum worker in mining.cpp.
enum PoolPhase {
  POOL_PHASE_IDLE = 0,      // not attempting yet (e.g. WiFi not ready)
  POOL_PHASE_CONNECTING,    // opening the TCP connection to the pool
  POOL_PHASE_RETRYING,      // last attempt failed; waiting before another try
  POOL_PHASE_SUBSCRIBING,   // socket up; Stratum subscribe/authorize handshake
  POOL_PHASE_AUTHORIZING,   // waiting for explicit mining.authorize response
  POOL_PHASE_AUTH_REJECTED, // pool rejected credentials
  POOL_PHASE_WAITING_JOB,   // subscribed; waiting for the first work item
  POOL_PHASE_MINING         // work is flowing — actually mining (the healthy state)
};

extern std::atomic<int>     g_poolPhase;       // current PoolPhase
extern std::atomic<uint8_t> g_poolRetryCount;  // consecutive failed connect attempts

