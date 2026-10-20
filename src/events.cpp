#include "events.h"

// =============================================================================
// Default weak implementations — all empty, zero overhead if not overridden.
//
// To handle any of these events, create a new .cpp file in src/ and define
// the function(s) you want. See events.h for full documentation and examples.
// =============================================================================

__attribute__((weak)) void onBlockFound() {}
__attribute__((weak)) void onShareAccepted(double difficulty) { (void)difficulty; }
__attribute__((weak)) void onShareRejected() {}
__attribute__((weak)) void onNewJobReceived() {}
__attribute__((weak)) void onMiningStarted() {}
