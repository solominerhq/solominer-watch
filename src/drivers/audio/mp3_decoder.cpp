// Use the pinned upstream CC0 implementation directly, with no Helix wrapper.
// The scalar path is shared by ESP32-S3 and host tests.
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "mp3_decoder.h"

namespace audio_decode {

Mp3Decoder::Mp3Decoder(const uint8_t* source, size_t size)
    : source_(source), size_(size) {
    mp3dec_init(&decoder_);
}

Result Mp3Decoder::next(Frame& frame) {
    frame = Frame{};
    if (position_ >= size_) return Result::End;
    if (!source_) return Result::Invalid;
    const size_t remaining = size_ - position_;
    const size_t window = remaining < InputWindowBytes ? remaining : InputWindowBytes;
    mp3dec_frame_info_t info{};
    const int samples = mp3dec_decode_frame(&decoder_, source_ + position_,
                                            int(window), pcm_, &info);
    if (info.frame_bytes < 0 || size_t(info.frame_bytes) > window ||
            samples < 0 || samples > MaxSamplesPerChannel) return Result::Invalid;
    if (samples == 0) {
        // No complete frame found. Keep lookahead across a scan-window boundary
        // so a frame split there is not lost. Always advance or terminate.
        if (info.frame_bytes == 0 || size_t(info.frame_bytes) == window) {
            position_ += remaining > window ? window - 4096 : window;
        } else {
            position_ += size_t(info.frame_bytes);
        }
        return Result::Skipped;
    }
    if (info.frame_bytes == 0 || info.hz != SampleRate || info.layer != 3 ||
            (info.channels != 1 && info.channels != 2)) return Result::Invalid;
    position_ += size_t(info.frame_bytes);
    frame.pcm = pcm_;
    frame.samplesPerChannel = samples;
    frame.channels = info.channels;
    return Result::Frame;
}

} // namespace audio_decode
