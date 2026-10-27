#include "diagnostics/diagnostics.h"
#include <atomic>
#include <memory>
#include <new>
// Block-found celebration audio for the Waveshare ESP32-S3-Touch-AMOLED-2.06.
//
// Decodes an optional local MP3 (mono/stereo 16 kHz) with the CC0 minimp3 decoder and
// streams the PCM to the onboard ES8311 codec + speaker: I2S with MCLK on
// GPIO16, codec at I2C 0x18, speaker amp/PA on GPIO46 (from Waveshare's
// 08_ES8311 sample). Playback-only, so the board's mic+speaker shared-I2S
// limitation doesn't apply.
//
// NOTE ON API: this project is pinned to arduino-esp32 core 2.0.17 (IDF 4.4),
// whereas Waveshare's sample targets core 3.x. So we use the legacy
// driver/i2s.h API (not ESP_I2S/I2SClass) and drive the ES8311's I2C over
// Arduino Wire (the same shared bus as touch/RTC/PMU), serialized with the
// display driver's I2C lock.
//
// Lazy + self-contained: each play brings up the codec + I2S on a dedicated
// task and streams the clip. The amplifier is switched off afterward; codec
// and I2S remain initialized for reliable replay. Diagnostics records this.

#include "audio.h"
#include <Arduino.h>
#include <Wire.h>
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "es8311.h"
}
#include "celebration_track.h"
#include "mp3_decoder.h"

// I2C lock shared with the display driver (touch/RTC/PMU live on the same bus).
extern bool displayI2cLock(uint32_t timeoutMs);
extern void displayI2cUnlock();

// --- Board audio wiring (from Waveshare's 08_ES8311 sample) ---
#define AUD_BCLK      41
#define AUD_WS        45
#define AUD_DOUT      40   // ESP -> codec (speaker)
#define AUD_MCLK      16   // REQUIRED — codec is silent without it
#define AUD_PA        46   // speaker amplifier enable, active HIGH
#define AUD_SAMPLE_HZ 16000
#define AUD_VOLUME    69   // 0-100
#define AUD_I2S_PORT  I2S_NUM_0
#define AUD_DMA_BUFFERS 8
#define AUD_DMA_FRAMES 256

static std::atomic<bool> s_playing{false};
static std::atomic<bool> s_stop{false};
static std::atomic<bool> s_muted{false};
static std::atomic<bool> s_prewarming{false};
static std::atomic_flag s_initializing = ATOMIC_FLAG_INIT;
static bool s_i2sStopped=false; // protected by s_initializing
static std::atomic<bool> s_hwReady{false};   // codec + I2S installed once, kept up
static std::atomic<bool> s_clockStarted{false};
static std::atomic<uint32_t> s_startedMs{0};
static std::atomic<uint32_t> s_finishedPositionMs{0};

// ---- ES8311 I2C transport over Arduino Wire (called by es8311.c) ----------
extern "C" int es8311_hw_i2c_write(uint16_t dev_addr, const uint8_t *buf, unsigned int len) {
    Wire.beginTransmission((uint8_t)dev_addr);
    Wire.write(buf, len);
    return (Wire.endTransmission() == 0) ? 0 : -1;
}
extern "C" int es8311_hw_i2c_read(uint16_t dev_addr, uint8_t reg_addr, uint8_t *value) {
    Wire.beginTransmission((uint8_t)dev_addr);
    Wire.write(reg_addr);
    if (Wire.endTransmission(false) != 0) return -1;                  // repeated start
    if (Wire.requestFrom((uint8_t)dev_addr, (uint8_t)1) != 1) return -1;
    *value = Wire.read();
    return 0;
}

static bool es8311_bringup() {
    es8311_handle_t es = es8311_create(0 /*unused*/, ES8311_ADDRESS_0);
    if (!es) return false;
    bool ok = false;
    if (displayI2cLock(300)) {
        es8311_clock_config_t clk;
        clk.mclk_inverted      = false;
        clk.sclk_inverted      = false;
        clk.mclk_from_mclk_pin = true;
        clk.mclk_frequency     = AUD_SAMPLE_HZ * 256;
        clk.sample_frequency   = AUD_SAMPLE_HZ;
        ok = es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) == ESP_OK &&
             es8311_microphone_config(es, false) == ESP_OK &&
             es8311_voice_volume_set(es, AUD_VOLUME, NULL) == ESP_OK &&
             es8311_voice_mute(es, false) == ESP_OK;
        displayI2cUnlock();
    }
    if (ok) { es8311_delete(es); return true; }   // registers persist; free handle
    // On failure try to leave it muted.
    if (displayI2cLock(200)) { es8311_voice_mute(es, true); displayI2cUnlock(); }
    es8311_delete(es);
    return false;
}

static bool i2s_bringup();   // defined below

// Bring the codec + I2S up ONCE and keep them up for the life of the boot.
// Repeated install/uninstall of the legacy I2S driver with APLL is unreliable
// (the clock frequently fails to re-init), which is why a second celebration
// produced no sound. Initializing once and simply re-writing the PCM on replay
// fixes that; the speaker amp (GPIO46) is toggled per play so nothing is
// audible between celebrations.
static bool ensureAudioHw() {
    // Prewarming and direct playback can overlap; only one task initializes I2S.
    while (s_initializing.test_and_set()) vTaskDelay(1);
    const bool alreadyReady = s_hwReady.load();
    const bool ready = alreadyReady || (es8311_bringup() && i2s_bringup());
    s_hwReady.store(ready);
    bool running=ready;
    if (ready && s_i2sStopped) {
        running=i2s_start(AUD_I2S_PORT)==ESP_OK;
        if (running) s_i2sStopped=false;
    }
    s_initializing.clear();
    if (ready && !alreadyReady) diagnostics::event(diagnostics::Event::AudioReady);
    return running;
}

static bool i2s_bringup() {
    i2s_config_t cfg = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = AUD_SAMPLE_HZ;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT;   // stereo (L=R written)
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = 0;
    cfg.dma_buf_count        = AUD_DMA_BUFFERS;
    cfg.dma_buf_len          = AUD_DMA_FRAMES;
    cfg.use_apll             = true;                         // accurate audio MCLK
    cfg.tx_desc_auto_clear   = true;
    cfg.fixed_mclk           = AUD_SAMPLE_HZ * 256;
    cfg.mclk_multiple        = I2S_MCLK_MULTIPLE_256;
    cfg.bits_per_chan        = I2S_BITS_PER_CHAN_16BIT;

    if (i2s_driver_install(AUD_I2S_PORT, &cfg, 0, NULL) != ESP_OK) return false;

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = AUD_MCLK;
    pins.bck_io_num   = AUD_BCLK;
    pins.ws_io_num    = AUD_WS;
    pins.data_out_num = AUD_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(AUD_I2S_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(AUD_I2S_PORT);
        return false;
    }
    i2s_zero_dma_buffer(AUD_I2S_PORT);
    return true;
}

// Decoded frame → I2S. `frames` is samples PER CHANNEL, not total samples.
// Mono is duplicated to L/R; stereo PCM is already interleaved.
static void bfWritePcm(const int16_t* pcm, size_t frames, int channels) {
    if (s_stop || frames == 0) return;
    if (!pcm || frames > 1152 || (channels != 1 && channels != 2)) {
        diagnostics::event(diagnostics::Event::AudioDecodeFailed);
        s_stop=true;digitalWrite(AUD_PA,LOW);return;
    }
    // Gate the amplifier, not the decoder/clock: mute keeps the beat in sync.
    digitalWrite(AUD_PA, s_muted.load() ? LOW : HIGH);
    const uint32_t frameStart=millis();
    static int16_t stereo[2304];   // one MP3 frame (mono 1152 -> 2304 interleaved)
    const void* output=pcm;
    size_t bytes=frames*size_t(channels)*sizeof(int16_t), w=0;
    if (channels == 1) {
        for (size_t k = 0; k < frames; k++) { stereo[k * 2] = pcm[k]; stereo[k * 2 + 1] = pcm[k]; }
        output=stereo;bytes=frames*2*sizeof(int16_t);
    }
    if(i2s_write(AUD_I2S_PORT,output,bytes,&w,pdMS_TO_TICKS(500))!=ESP_OK || w!=bytes){
        diagnostics::event(diagnostics::Event::AudioOutputFailed);
        s_stop=true;digitalWrite(AUD_PA,LOW);return;
    }
    // Do not report a running song when DMA rejected the first frame.
    if (!s_clockStarted.load()) {
        s_startedMs.store(frameStart);
        s_clockStarted.store(true);
    }
}

// Legacy I2S has no wait-until-empty API. Push one complete DMA ring plus one
// buffer of silence before switching off the amplifier so the final music
// samples actually reach the speaker. Explicit close skips this bounded drain.
static void drainAudioTail() {
    static const int16_t silence[AUD_DMA_FRAMES * 2] = {};
    for (unsigned i=0;i<AUD_DMA_BUFFERS+1 && !s_stop.load();++i) {
        digitalWrite(AUD_PA,s_muted.load()?LOW:HIGH);
        size_t written=0;
        if (i2s_write(AUD_I2S_PORT,silence,sizeof(silence),&written,
                      pdMS_TO_TICKS(500))!=ESP_OK || written!=sizeof(silence)) {
            diagnostics::event(diagnostics::Event::AudioOutputFailed);
            s_stop=true;break;
        }
    }
}

static void audioTask(void *arg) {
    (void)arg;

    bool ready=false;
    for(unsigned attempt=0;attempt<3 && !s_stop.load();++attempt){
        if(ensureAudioHw()){ready=true;break;}
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    if (!ready) {
        if(!s_stop.load()) diagnostics::event(diagnostics::Event::AudioInitFailed);
        pinMode(AUD_PA,OUTPUT);digitalWrite(AUD_PA,LOW);
        s_stop=false;
        s_playing = false;
        vTaskDelete(NULL);
        return;
    }

    pinMode(AUD_PA, OUTPUT);
    i2s_zero_dma_buffer(AUD_I2S_PORT);   // clear any stale tail from a prior play
    diagnostics::event(diagnostics::Event::AudioStart);
    digitalWrite(AUD_PA, (!s_muted.load() && !s_stop.load()) ? HIGH : LOW);

    // Decode one frame at a time. Blocking I2S writes pace the pipeline, while
    // mute keeps decoding and timing active. Each replay gets fresh decoder state.
    {
        std::unique_ptr<audio_decode::Mp3Decoder> mp3(
            new(std::nothrow) audio_decode::Mp3Decoder(celebrationTrackData(),celebrationTrackSize()));
        if(!mp3) diagnostics::event(diagnostics::Event::AudioDecodeFailed);
        uint32_t lastPcm=millis();
        while (mp3 && !s_stop.load()) {
            if(millis()-lastPcm>2000){
                diagnostics::event(diagnostics::Event::AudioDecodeFailed);
                s_stop=true;break;
            }
            audio_decode::Frame frame;
            const auto result=mp3->next(frame);
            if (result==audio_decode::Result::End) break;
            if (result==audio_decode::Result::Invalid) {
                diagnostics::event(diagnostics::Event::AudioDecodeFailed);
                s_stop=true;break;
            }
            if (result==audio_decode::Result::Skipped) { vTaskDelay(1);continue; }
            bfWritePcm(frame.pcm,size_t(frame.samplesPerChannel),frame.channels);
            lastPcm=millis();
        }
        if(mp3 && !s_clockStarted.load() && !s_stop.load())
            diagnostics::event(diagnostics::Event::AudioDecodeFailed);
    }
    if(s_clockStarted.load() && !s_stop.load()) drainAudioTail();

    // Amp off (silence between celebrations). Leave the I2S driver + codec
    // installed so the next play works — see ensureAudioHw(). Flush the DMA so
    // the tail doesn't linger.
    digitalWrite(AUD_PA, LOW);
    diagnostics::event(diagnostics::Event::AudioStop);
    i2s_zero_dma_buffer(AUD_I2S_PORT);

    if (s_clockStarted.load()) s_finishedPositionMs.store(uint32_t(millis()-s_startedMs.load()));
    s_stop = false;
    s_playing = false;
    vTaskDelete(NULL);
}

// Bring the codec + I2S up ahead of the celebration on a throwaway task, so the
// first audioPlayBlockFound() after a boot doesn't pay the ~100-200ms hardware
// init and lag behind the animation. Runs off the render task so the UI doesn't
// hitch. No-op once the hardware is already up.
static void prewarmTask(void *arg) {
    (void)arg;
    ensureAudioHw();
    s_prewarming=false;
    vTaskDelete(NULL);
}

bool audioHasBlockFoundTrack() { return celebrationTrackSize() != 0; }

void audioPrewarm() {
    if (!audioHasBlockFoundTrack() || s_hwReady || s_playing) return;
    bool expected=false;
    if(!s_prewarming.compare_exchange_strong(expected,true)) return;
    if(xTaskCreatePinnedToCore(prewarmTask,"bfWarm",8192,NULL,3,NULL,0)!=pdPASS)
        s_prewarming=false; // Direct playback can still initialize the hardware.
}

void audioPlayBlockFound() {
    if (!audioHasBlockFoundTrack()) return;
    bool expected=false;
    if(!s_playing.compare_exchange_strong(expected,true)) return;
    s_clockStarted.store(false);
    s_finishedPositionMs.store(0);
    s_stop = false;
    // Core 0 (the HW-SHA miner runs pinned to core 1). Generous stack for the
    // I2S driver bring-up and minimp3's ~17 KB temporary decode workspace.
    // Decoder history / output frame live on the heap, not on this stack.
    BaseType_t ok = xTaskCreatePinnedToCore(audioTask, "bfAudio", 32768, NULL, 4, NULL, 0);
    if (ok != pdPASS) {
        diagnostics::event(diagnostics::Event::AudioTaskFailed);
        s_playing = false;   // couldn't start — don't wedge future plays
    }
}

void audioStopBlockFound() {
    if (s_playing) s_stop = true;
}

bool audioIsPlaying(){return s_playing.load();}
bool audioHardwareReady(){return s_hwReady.load();}

bool audioBlockFoundPositionMs(uint32_t& position) {
    if (!s_clockStarted.load()) return false;
    position = s_playing.load() ? uint32_t(millis()-s_startedMs.load()) : s_finishedPositionMs.load();
    return true;
}

void audioSetBlockFoundMuted(bool muted) { s_muted.store(muted); }
bool audioBlockFoundMuted() { return s_muted.load(); }

// Called with application publishers parked. Keep init/playback serialized until
// the sleep owner releases this guard; never stop DMA under an active decoder.
bool audioPrepareLightSleep() {
    if (s_playing.load() || s_initializing.test_and_set()) return false;
    if (s_playing.load()) { s_initializing.clear(); return false; }
    if (s_hwReady.load() && !s_i2sStopped) {
        if (i2s_stop(AUD_I2S_PORT)!=ESP_OK) { s_initializing.clear(); return false; }
        s_i2sStopped=true;
    }
    pinMode(AUD_PA,OUTPUT);digitalWrite(AUD_PA,LOW);
    return true;
}
void audioFinishLightSleep() { s_initializing.clear(); }
