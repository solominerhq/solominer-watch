"""Run production playback tasks with fault-injected codec, decoder and task I/O."""
from pathlib import Path
import os, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>
#define AUD_PA 46
#define AUD_DMA_BUFFERS 8
#define AUD_DMA_FRAMES 256
#define AUD_I2S_PORT 0
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define ESP_OK 0
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
using BaseType_t=int;
static uint32_t now=0;
uint32_t millis(){return now;}
void vTaskDelay(int ticks){now+=ticks;}
void vTaskDelete(void*){}
void pinMode(int,int){}
static int amp=0,initFailures=0,initCalls=0,writes=0,taskCalls=0,decoderEnds=0,drainWrites=0;
static bool closeDuringDrain=false;
void audioStopBlockFound();
static bool taskFailure=false,decoderFailure=false,decoderStall=false,writeFailure=false;
void digitalWrite(int,int value){amp=value;}
bool es8311_bringup(){++initCalls;if(initFailures){--initFailures;return false;}return true;}
bool i2s_bringup(){return true;}
int i2s_start(int){return ESP_OK;}int i2s_stop(int){return ESP_OK;}
void i2s_zero_dma_buffer(int){}
int i2s_write(int,const void* data,size_t n,size_t* w,int){
 if(n==AUD_DMA_FRAMES*2*sizeof(int16_t)){
  ++drainWrites;auto* pcm=static_cast<const int16_t*>(data);
  for(unsigned i=0;i<n/sizeof(int16_t);++i)assert(pcm[i]==0);
  if(closeDuringDrain)audioStopBlockFound();
 }else ++writes;
 *w=writeFailure?0:n;now+=36;return writeFailure?-1:ESP_OK;
}
namespace diagnostics {
enum class Event{AudioReady,AudioStart,AudioStop,AudioInitFailed,AudioDecodeFailed,AudioOutputFailed,AudioTaskFailed};
static std::vector<Event> events;
void event(Event e){events.push_back(e);}
int count(Event e){int n=0;for(auto x:events)if(x==e)++n;return n;}
}
using Task=void(*)(void*);
static std::vector<Task> queued;
int xTaskCreatePinnedToCore(Task task,const char*,int,void*,int,void*,int){
 ++taskCalls;if(taskFailure)return 0;queued.push_back(task);return pdPASS;
}
static unsigned char localTrack[2048]={};
static bool hasTrack=true;
const uint8_t* celebrationTrackData(){return hasTrack?localTrack:nullptr;}
size_t celebrationTrackSize(){return hasTrack?sizeof(localTrack):0;}
namespace audio_decode {
enum class Result {Frame,Skipped,End,Invalid};
struct Frame{const int16_t* pcm=nullptr;int samplesPerChannel=0;int channels=0;};
class Mp3Decoder {
 unsigned frames=0;
 public:
 Mp3Decoder(const uint8_t*,size_t){}
 ~Mp3Decoder(){++decoderEnds;}
 Result next(Frame& frame){
  if(decoderFailure)return Result::Invalid;
  if(decoderStall)return Result::Skipped;
  if(frames++==2)return Result::End;
  static short pcm[]={1,-1,3,-3};frame.pcm=pcm;frame.samplesPerChannel=4;frame.channels=1;
  return Result::Frame;
 }
};
}
'''
CHECKS = r'''
static void run(){assert(!queued.empty());auto f=queued.front();queued.erase(queued.begin());f(nullptr);}
int main(){
 using diagnostics::Event;
 // No track: no hardware init, task, error or fake audio clock, on repeated runs.
 hasTrack=false;
 for(int i=0;i<3;++i){audioPrewarm();audioPlayBlockFound();audioStopBlockFound();}
 uint32_t position=99;
 assert(!audioHasBlockFoundTrack()&&!audioIsPlaying()&&!audioBlockFoundPositionMs(position));
 assert(taskCalls==0&&initCalls==0&&queued.empty()&&diagnostics::events.empty());
 hasTrack=true;assert(audioHasBlockFoundTrack());
 // A prewarm failure must not prevent a direct play; retry transient I2C failure.
 taskFailure=true;audioPrewarm();assert(!s_prewarming);taskFailure=false;
 audioPrewarm();audioPrewarm();assert(queued.size()==1);initFailures=1;run();assert(!s_hwReady);
 initFailures=1;audioPlayBlockFound();audioPlayBlockFound();assert(queued.size()==1);
 run();assert(!audioIsPlaying()&&s_clockStarted&&writes==2&&amp==LOW&&initCalls==3);
 assert(diagnostics::count(Event::AudioInitFailed)==0&&drainWrites==AUD_DMA_BUFFERS+1);
 // Subsequent replay needs no hardware reinstall, including a muted run.
 int installs=initCalls;audioSetBlockFoundMuted(true);audioPlayBlockFound();run();
 assert(audioBlockFoundMuted()&&s_clockStarted&&initCalls==installs&&writes==4);
 // Invalid input and a stalled decoder both terminate, rather than
 // leaving s_playing true and holding the display awake indefinitely.
 decoderFailure=true;audioPlayBlockFound();run();assert(!s_playing&&!s_clockStarted&&amp==LOW);
 decoderFailure=false;decoderStall=true;audioPlayBlockFound();run();
 assert(!s_playing&&!s_clockStarted&&amp==LOW&&diagnostics::count(Event::AudioDecodeFailed)==2);
 decoderStall=false;audioSetBlockFoundMuted(false);audioPlayBlockFound();run();assert(s_clockStarted);
 // Exhausted hardware retries leave future requests usable.
 s_hwReady=false;initFailures=3;audioPlayBlockFound();run();
 assert(!s_playing&&!s_clockStarted&&amp==LOW&&diagnostics::count(Event::AudioInitFailed)==1);
 audioPlayBlockFound();run();assert(s_hwReady&&s_clockStarted);
 // Output failure does not report an audible song and exits promptly.
 writeFailure=true;audioPlayBlockFound();run();
 assert(!s_playing&&!s_clockStarted&&amp==LOW&&diagnostics::count(Event::AudioOutputFailed)==1);
 writeFailure=false;
 // Failed task creation and closing before task dispatch never wedge a replay.
 taskFailure=true;audioPlayBlockFound();assert(!s_playing&&diagnostics::count(Event::AudioTaskFailed)==1);
 taskFailure=false;audioPlayBlockFound();audioStopBlockFound();run();assert(!s_playing&&!s_stop&&!s_clockStarted);
 audioPlayBlockFound();run();assert(s_clockStarted&&!s_playing&&amp==LOW);
 // Closing during the tail is prompt, and a later replay starts cleanly.
 int beforeDrain=drainWrites;closeDuringDrain=true;audioPlayBlockFound();run();
 assert(drainWrites==beforeDrain+1&&!s_stop&&!s_playing&&amp==LOW);
 closeDuringDrain=false;audioPlayBlockFound();run();
 assert(drainWrites==beforeDrain+1+AUD_DMA_BUFFERS+1&&!s_playing);
}
'''

class AudioPlayback(unittest.TestCase):
    def test_start_replay_and_failure_recovery(self):
        source = (ROOT/'src/drivers/audio/audio.cpp').read_text()
        def part(a,b):
            start=source.index(a)
            return source[start:source.index(b,start)]
        code=STUBS+part('static std::atomic<bool> s_playing','// ---- ES8311')
        code+=part('static bool ensureAudioHw()', 'static bool i2s_bringup() {')
        code+=source[source.index('static void bfWritePcm('):]+CHECKS
        with tempfile.TemporaryDirectory() as tmp:
            cpp=Path(tmp)/'test.cpp';binary=Path(tmp)/'test'
            cpp.write_text(code)
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)
