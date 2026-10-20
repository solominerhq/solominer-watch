"""Keep grounded astronaut poses stationary while the dust/headline animate."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_celebration_replay import function

ROOT = Path(__file__).resolve().parents[1]


class LandingMotion(unittest.TestCase):
    def test_grounded_poses_do_not_skate_or_wobble(self):
        source = (ROOT / 'src/drivers/displays/block_found_scene.h').read_text()
        stubs = r'''
#include <cassert>
#include <cstdint>
#include <cmath>
#include "ui/block_found_sequence.h"
using lv_layer_t=int;
struct State { uint32_t elapsed=0,color=0; bool countdown=false,fullSequence=true; } state;
using lv_event_t=int;
void* lv_event_get_user_data(lv_event_t*){return &state;}
lv_layer_t* lv_event_get_layer(lv_event_t*){return nullptr;}
struct lv_image_dsc_t { struct {int w=100;} header; };
static lv_image_dsc_t mission_moon,mission_block,mission_found;
static constexpr int sceneryOffsetY=10;
namespace clock_stars{void paint(lv_layer_t*){}}
template<class... T>void image(T...){}
template<class... T>void dust(T...){}
template<class... T>void ellipse(T...){}
void countdownPaint(lv_layer_t*,uint32_t){}
static int pose=-1,ground=0,anchor=0;
static float angle=0,scale=0;
void body(lv_layer_t*,int index,int foot=398,float rotation=0,float squash=1,int x=206){
 pose=index;ground=foot;anchor=x;angle=rotation;scale=squash;
}
'''
        code = stubs + '\n'.join(function(source, name) for name in
                                  ['inline float clamp(', 'inline float ease(',
                                   'inline float smooth(', 'inline void paint('])
        code += r'''
int main(){
 using namespace ui::block_found;
 int previousGround=0;
 for(state.elapsed=0;state.elapsed<touchdownMs;++state.elapsed){
  paint(nullptr);assert(pose==0&&ground>=previousGround&&ground<=398);previousGround=ground;
 }
 // One landing/crouch/strike sequence, with no horizontal sliding, whole-body
 // tilt or stretching once a foot has contacted the moon.
 for(state.elapsed=touchdownMs;state.elapsed<detailsMs;++state.elapsed){
  paint(nullptr);assert(ground==398&&angle==0&&scale==1);
  if(state.elapsed<windupMs)assert(pose==1&&anchor==206);
  else if(state.elapsed<impactMs)assert(pose==2&&anchor==206);
  else if(state.elapsed<dotsMs)assert(pose==3&&anchor==250);
  else assert(pose==4&&anchor==250);
 }
 state.fullSequence=false;state.elapsed=detailsMs;paint(nullptr);
 assert(pose==4&&ground==398&&angle==0&&scale==1&&anchor==250);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path/'test.cpp').write_text(code)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                            '-Werror', '-I'+str(ROOT/'src'), str(path/'test.cpp'),
                            '-o', str(path/'test')], check=True)
            subprocess.run([str(path/'test')], check=True, timeout=10)
