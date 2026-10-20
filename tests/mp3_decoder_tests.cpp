#include "drivers/audio/mp3_decoder.h"
#include <assert.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <vector>
#include <cstring>
#include <cmath>

using Bytes = std::vector<uint8_t>;
struct Decoded {
    std::vector<int16_t> samples;
    size_t frames = 0, steps = 0;
    int channels = 0;
    bool invalid = false;
};
static Bytes read(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    return Bytes(std::istreambuf_iterator<char>(file), {});
}
static Decoded decode(const Bytes& bytes) {
    std::unique_ptr<audio_decode::Mp3Decoder> decoder(
        new audio_decode::Mp3Decoder(bytes.data(), bytes.size()));
    Decoded result;
    size_t previous = 0;
    for (;;) {
        assert(++result.steps < bytes.size() + 3); // bounded even for malformed input
        audio_decode::Frame frame;
        const auto status = decoder->next(frame);
        if (status == audio_decode::Result::End) break;
        if (status == audio_decode::Result::Invalid) { result.invalid=true;break; }
        assert(decoder->consumed() > previous && decoder->consumed() <= bytes.size());
        previous = decoder->consumed();
        if (status == audio_decode::Result::Skipped) { assert(!frame.pcm);continue; }
        assert(frame.pcm && frame.samplesPerChannel > 0 &&
               frame.samplesPerChannel <= 1152 && (frame.channels == 1 || frame.channels == 2));
        result.channels=frame.channels;
        result.frames+=size_t(frame.samplesPerChannel);
        result.samples.insert(result.samples.end(), frame.pcm,
                              frame.pcm + frame.samplesPerChannel * frame.channels);
    }
    return result;
}
static void tone(const Decoded& out, int channels, int hz) {
    assert(!out.invalid && out.channels == channels);
    assert(out.frames >= 8000 && out.frames <= 10000);
    assert(out.samples.size() == out.frames * size_t(channels));
    // Ignore encoder lead-in/tail and check signal energy/frequency.
    double energy=0; int crossings=0;
    for (size_t n=2000;n<7000;++n) {
        int value=out.samples[n*channels];
        energy+=double(value)*value;
        if (value>0 && out.samples[(n-1)*channels]<=0) ++crossings;
        if (channels==2) assert(std::abs(value-out.samples[n*channels+1])<20);
    }
    assert(energy/5000 > 1000000);
    assert(std::abs(crossings - hz*5000.0/16000) < 3);
}
int main(int argc, char** argv) {
    if (argc==4 && !std::strcmp(argv[1],"--decode")) {
        auto out=decode(read(argv[2]));
        assert(!out.invalid && out.frames);
        std::ofstream file(argv[3],std::ios::binary);
        file.write(reinterpret_cast<const char*>(out.samples.data()),
                   std::streamsize(out.samples.size()*sizeof(int16_t)));
        assert(file.good());
        std::cout << "frames=" << out.frames << " channels=" << out.channels
                  << " seconds=" << out.frames/16000.0 << "\n";
        return 0;
    }
    assert(argc==4);
    const Bytes mono=read(argv[1]), stereo=read(argv[2]), unsupported=read(argv[3]);
    auto first=decode(mono);
    tone(first,1,440); tone(decode(stereo),2,660);
    // Fresh state for replay: identical PCM, including after a rejected file.
    assert(decode(unsupported).invalid);
    assert(decode(mono).samples==first.samples);
    assert(decode({}).samples.empty());
    assert(decode(Bytes(50000,0)).samples.empty());
    // Headers split over the 16 KB input window must not lose initial frames.
    for (size_t padding : {size_t(16380),size_t(32760),size_t(50000)}) {
        Bytes prefixed(padding,0);
        prefixed.insert(prefixed.end(),mono.begin(),mono.end());
        assert(decode(prefixed).samples==first.samples);
    }
    for (size_t size : {size_t(1),size_t(3),size_t(8),size_t(100),mono.size()-1}) {
        auto truncated=decode(Bytes(mono.begin(),mono.begin()+size));
        assert(truncated.frames<=first.frames);
    }
    // Deterministic malformed inputs exercise synchronization and termination.
    uint32_t random=123;
    for (unsigned i=0;i<40;++i) {
        Bytes damaged(700+i*431);
        for (auto& byte : damaged) { random=random*1664525u+1013904223u;byte=random>>24; }
        decode(damaged);
    }
    audio_decode::Mp3Decoder nullSource(nullptr,12);
    audio_decode::Frame frame;
    assert(nullSource.next(frame)==audio_decode::Result::Invalid);
    std::cout << "PASS: real mono/stereo MP3, replay, unsupported rate, scan boundaries, truncated and malformed input\n";
}
