#pragma once
#include <stddef.h>
#include <stdint.h>
#include "third_party/minimp3/minimp3.h"

namespace audio_decode {

// The speaker pipeline is configured for 16 kHz PCM. Reject other rates rather
// than silently playing them at the wrong speed. Mono and stereo are supported.
constexpr int SampleRate = 16000;
constexpr int MaxSamplesPerChannel = 1152;
constexpr size_t InputWindowBytes = 16384;

enum class Result { Frame, Skipped, End, Invalid };
struct Frame {
    const int16_t* pcm = nullptr;
    int samplesPerChannel = 0;
    int channels = 0;
};

// Allocate on the heap, not the audio task stack: this owns decoder state and a
// complete PCM frame. The source bytes must remain alive for the whole session.
class Mp3Decoder {
public:
    Mp3Decoder(const uint8_t* source, size_t size);
    Result next(Frame& frame);
    size_t consumed() const { return position_; }
private:
    mp3dec_t decoder_{};
    mp3d_sample_t pcm_[MINIMP3_MAX_SAMPLES_PER_FRAME];
    const uint8_t* source_;
    size_t size_;
    size_t position_ = 0;
};

} // namespace audio_decode
