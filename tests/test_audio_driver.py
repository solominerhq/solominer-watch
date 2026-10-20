"""Exercise production audio initialization, PCM mute and song clock with fake I/O."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>
#define AUD_PA 46
#define AUD_I2S_PORT 0
#define HIGH 1
#define OUTPUT 1
#define ESP_OK 0
void pinMode(int,int){}
static int starts=0,stops=0;static bool failStart=false,failStop=false;
int i2s_start(int){++starts;return failStart?-1:0;}
int i2s_stop(int){++stops;return failStop?-1:0;}
#define LOW 0
#define pdMS_TO_TICKS(x) (x)
static uint32_t now=0;
uint32_t millis(){return now;}
void vTaskDelay(int){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
static std::atomic<int> codecCalls{0},i2sCalls{0},readyEvents{0};
static bool bringupOk=true;
static bool es8311_bringup(){++codecCalls;std::this_thread::sleep_for(std::chrono::milliseconds(4));return bringupOk;}
static bool i2s_bringup(){++i2sCalls;return true;}
namespace diagnostics {enum class Event{AudioReady,AudioOutputFailed,AudioDecodeFailed};void event(Event e){if(e==Event::AudioReady)++readyEvents;}}
static std::vector<int> amp;
static std::vector<int16_t> output;
static unsigned writes=0;static bool failWrite=false,shortWrite=false;
void digitalWrite(int pin,int value){assert(pin==AUD_PA);amp.push_back(value);}
int i2s_write(int,const void* data,size_t bytes,size_t* written,int){
 ++writes;*written=shortWrite?bytes/2:bytes;auto* pcm=static_cast<const int16_t*>(data);
 output.assign(pcm,pcm+bytes/sizeof(int16_t));return failWrite?-1:ESP_OK;
}
'''
CHECKS = r'''
int main(){
 assert(!s_prewarming.load());
 // Concurrent prewarm/play requests must not install I2S twice.
 std::vector<std::thread> workers;
 for(int i=0;i<8;++i)workers.emplace_back([]{assert(ensureAudioHw());});
 for(auto& worker:workers)worker.join();
 assert(codecCalls==1&&i2sCalls==1&&readyEvents==1&&audioHardwareReady());
 // A failed first initialization releases the lock and remains retryable.
 s_hwReady=false;bringupOk=false;assert(!ensureAudioHw());
 bringupOk=true;assert(ensureAudioHw());assert(codecCalls==3&&i2sCalls==2&&readyEvents==2);
 uint32_t position=999;assert(!audioBlockFoundPositionMs(position));
 short pcm[]={12,-34,56};
 s_playing=true;now=100;bfWritePcm(pcm,3,1);
 assert(writes==1&&amp.back()==HIGH&&output==std::vector<int16_t>({12,12,-34,-34,56,56}));
 assert(audioBlockFoundPositionMs(position)&&position==0);
 audioSetBlockFoundMuted(true);now=800;bfWritePcm(pcm,3,1);
 assert(audioBlockFoundMuted()&&amp.back()==LOW&&writes==2&&s_startedMs==100);
 assert(audioBlockFoundPositionMs(position)&&position==700&&audioIsPlaying());
 audioSetBlockFoundMuted(false);now=1400;bfWritePcm(pcm,1,2);
 assert(!audioBlockFoundMuted()&&amp.back()==HIGH&&writes==3&&output==std::vector<int16_t>({12,-34}));
 assert(audioBlockFoundPositionMs(position)&&position==1300&&s_startedMs==100);
 // Stopping prevents further PCM writes; zero-length callbacks do nothing.
 bfWritePcm(pcm,0,1);assert(writes==3);
 audioStopBlockFound();assert(s_stop);bfWritePcm(pcm,3,1);assert(writes==3);
 s_playing=false;s_finishedPositionMs=1300;now=3000;
 assert(audioBlockFoundPositionMs(position)&&position==1300);
 // A rejected/partial first write must never start the song clock.
 s_stop=false;s_clockStarted=false;failWrite=true;bfWritePcm(pcm,3,1);
 assert(s_stop&&!s_clockStarted&&amp.back()==LOW);failWrite=false;s_stop=false;shortWrite=true;
 bfWritePcm(pcm,3,1);assert(s_stop&&!s_clockStarted&&amp.back()==LOW);
 shortWrite=false;s_stop=false;bfWritePcm(pcm,3,1);assert(s_clockStarted);
 // Idle sleep stops DMA once, keeps the amplifier off, and resumes on playback.
 s_playing=true;assert(!audioPrepareLightSleep());s_playing=false;
 failStop=true;assert(!audioPrepareLightSleep());failStop=false;
 assert(audioPrepareLightSleep()&&s_i2sStopped&&amp.back()==LOW);
 assert(!audioPrepareLightSleep()); // Guard is held until the sleep owner releases it.
 audioFinishLightSleep();assert(audioPrepareLightSleep());audioFinishLightSleep();assert(stops==2);
 failStart=true;assert(!ensureAudioHw()&&s_i2sStopped);failStart=false;
 assert(ensureAudioHw()&&!s_i2sStopped&&starts==2);
 // Feed an entire 130-second track: no visual cue may fade or stop PCM at 22 s.
 s_stop=false;s_clockStarted=false;s_playing=true;audioSetBlockFoundMuted(false);
 short fullFrame[1152];for(int i=0;i<1152;++i)fullFrame[i]=(i%2)?-12000:12000;
 const unsigned beforeFullSong=writes;const uint32_t songStart=20000;
 for(unsigned i=0;i<1810;++i){
  now=songStart+i*72;bfWritePcm(fullFrame,1152,1);
  assert(!s_stop&&writes==beforeFullSong+i+1&&output.size()==2304);
  for(int j=0;j<1152;++j)assert(output[j*2]==fullFrame[j]&&output[j*2+1]==fullFrame[j]);
 }
 assert(audioBlockFoundPositionMs(position)&&position==130248);
 audioStopBlockFound();bfWritePcm(fullFrame,1152,1);
 assert(writes==beforeFullSong+1810);
}
'''

class AudioDriverTests(unittest.TestCase):
    def test_mute_clock_and_serialized_initialization(self):
        source = (ROOT / 'src/drivers/audio/audio.cpp').read_text()
        def part(start, end):
            a = source.index(start)
            return source[a:source.index(end, a)]
        code = STUBS + part('static std::atomic<bool> s_playing', '// ---- ES8311')
        code += part('static bool ensureAudioHw()', 'static bool i2s_bringup() {')
        code += part('static void bfWritePcm(', 'static void drainAudioTail(')
        code += source[source.index('void audioStopBlockFound() {'):]
        with tempfile.TemporaryDirectory(prefix='solominer-audio-test-') as folder:
            cpp=Path(folder)/'audio.cpp'; binary=Path(folder)/'audio-test'
            cpp.write_text(code+CHECKS)
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++11','-Wall','-Wextra','-Werror','-pthread',str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)

if __name__ == '__main__':
    unittest.main()
