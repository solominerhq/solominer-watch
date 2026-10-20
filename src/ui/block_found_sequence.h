#pragma once
#include <stdint.h>

namespace ui { namespace block_found {
// Default mission cues at 90 BPM. See docs/CUSTOM_AUDIO.md to align another track.
// Uses the audio clock when a recording is supplied, otherwise the display clock.
static constexpr uint32_t beatsPerMinute = 90;
static_assert(beatsPerMinute > 0 && beatsPerMinute <= 300, "Choose a tempo from 1 to 300 BPM");
static constexpr uint32_t touchdownMs = 900;
static constexpr uint32_t windupMs = 1900;
static constexpr uint32_t impactMs = 2756;
static constexpr uint32_t revealMs = impactMs;
static constexpr uint32_t dotsMs = impactMs + 1333;
// Hold the settled astronaut for eight seconds before the one-time page tour.
// These visual cues never limit the song playback.
static constexpr uint32_t celebrationHoldMs = 8000;
static constexpr uint32_t detailsMs = dotsMs + celebrationHoldMs;
static constexpr uint32_t rewardHoldMs = 6000;
inline bool navigationReady(uint32_t elapsed) { return elapsed >= dotsMs; }
inline uint8_t nextPage(uint8_t page, bool forward) { return (page + (forward ? 1 : 2)) % 3; }
inline uint8_t landingFrame(uint32_t elapsed) {
    // The rejected third pose is deliberately omitted.
    return elapsed < touchdownMs ? 0 : elapsed < windupMs ? 1 :
           elapsed < impactMs ? 3 : elapsed < dotsMs ? 4 : 5;
}
// One automatic tour, canceled by intentional page navigation (never by mute).
struct Tour {
    bool manual = false;
    uint8_t stage = 0;
    uint32_t rewardStarted = 0;
    int update(uint32_t now, uint32_t elapsed) {
        if (manual) return -1;
        if (!stage && elapsed >= detailsMs) { stage=1; rewardStarted=now; return 1; }
        if (stage==1 && uint32_t(now-rewardStarted)>=rewardHoldMs) { stage=2; return 2; }
        return -1;
    }
};
inline uint32_t beatIndex(uint32_t elapsed) {
    return elapsed < impactMs ? 0 : uint32_t(uint64_t(elapsed - impactMs) * beatsPerMinute / 60000);
}
inline uint32_t beatAgeMs(uint32_t elapsed) {
    return elapsed < impactMs ? 0 : uint32_t(uint64_t(elapsed - impactMs) * beatsPerMinute % 60000) / beatsPerMinute;
}
// One audio-clock palette for every celebration heading. Only hue changes;
// no blank frames or opacity flashing. Saved records / finished music are orange.
static constexpr uint32_t headingOrange = 0xF97316;
inline uint32_t headingColor(uint32_t elapsed, bool playing) {
    if (!playing || elapsed < impactMs) return headingOrange;
    // Alternate on each musical beat, keeping the letters fully visible.
    return beatIndex(elapsed)%2 ? headingOrange : 0xFFFFFF;
}
inline bool validHash(const char* hash) {
    if (!hash) return false;
    for (unsigned i = 0; i < 64; ++i) {
        const char c = hash[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return hash[64] == '\0';
}
} }
