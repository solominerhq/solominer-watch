#pragma once
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <new>
#include "inter_fonts.h"
#include "block_found_assets.h"
#include "ui/block_found_sequence.h"
#include "ui/clock_stars.h"

namespace block_found_scene {
static constexpr int sceneryOffsetY = 10; // Lower the complete landing scene together.
inline float clamp(float x) { return x<0 ? 0 : x>1 ? 1 : x; }
inline float ease(float x) { const float a=1-clamp(x);return 1-a*a*a; }
inline float smooth(float x) { x=clamp(x);return x*x*(3-2*x); }
struct State { lv_obj_t* root; lv_timer_t* timer; uint32_t (*clock)(); uint32_t (*headingColor)(); void (*complete)(); uint32_t elapsed; uint32_t color; bool fullSequence; bool countdown; bool done; };
inline void rect(lv_layer_t* layer,int x,int y,int w,int h,uint32_t color,lv_opa_t opa=255,int radius=0) {
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=opa;d.radius=radius;
    const lv_area_t a={x,y,x+w-1,y+h-1};lv_draw_rect(layer,&d,&a);
}
inline void text(lv_layer_t* layer,const char* value,int baseline,uint32_t color,const lv_font_t* font) {
    lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.text=value;d.font=font;d.color=lv_color_hex(color);d.align=LV_TEXT_ALIGN_CENTER;
    const int top=baseline-font->line_height+font->base_line;
    const lv_area_t a={25,top,384,top+font->line_height-1};lv_draw_label(layer,&d,&a);
}
inline void image(lv_layer_t* layer,const lv_image_dsc_t* source,int x,int y,int px=0,int py=0,int sx=256,int sy=256,int rotation=0,uint32_t tint=0xFFFFFF) {
    lv_draw_image_dsc_t d;lv_draw_image_dsc_init(&d);d.src=source;d.scale_x=sx;d.scale_y=sy;d.pivot={px,py};d.rotation=rotation;d.antialias=1;d.recolor=lv_color_hex(tint);
    const lv_area_t a={x,y,x+source->header.w-1,y+source->header.h-1};lv_draw_image(layer,&d,&a);
}
inline void body(lv_layer_t* layer,int index,int foot=398,float rotation=0,float squash=1,int x=206) {
    static const lv_image_dsc_t* const poses[]={&mission_pose_0,&mission_pose_1,&mission_pose_3,&mission_pose_4,&mission_pose_5};
    static const int ax[]={95,87,99,162,137},ay[]={253,249,262,250,258};
    foot += sceneryOffsetY;
    image(layer,poses[index],x-ax[index],foot-ay[index],ax[index],ay[index],232,int(squash*232),int(rotation*1800/3.14159265f));
}
inline void ellipse(lv_layer_t* layer,float x,float y,float rx,float ry,uint32_t color,lv_opa_t opacity,int width) {
    lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.width=width;d.color=lv_color_hex(color);d.opa=opacity;d.round_start=d.round_end=1;
    for(int i=0;i<32;++i){const float a=i*6.2831853f/32,b=(i+1)*6.2831853f/32;
        d.p1={int(x+cosf(a)*rx),int(y+sinf(a)*ry)};d.p2={int(x+cosf(b)*rx),int(y+sinf(b)*ry)};lv_draw_line(layer,&d);}
}
inline void dust(lv_layer_t* layer,float age,int x=206,int y=398,bool impact=false) {
    if(age<0 || age>1.8f)return;
    y += sceneryOffsetY;
    static const uint32_t colors[]={0xFF8A21,0xFFAD5C,0xE9E5DC};
    for(int i=0;i<(impact?30:15);++i){const float a=i*2.399963f,v=(impact?54:25)+(i%7)*(impact?13:6);
        const int xx=int(x+cosf(a)*v*age),yy=int(y-fabsf(sinf(a))*v*age*.75f+34*age*age);
        const int rx=(impact?2:3)+i%3,ry=2+i%2;
        rect(layer,xx-rx,yy-ry,rx*2,ry*2,impact?colors[i%3]:0xE4E6E8,uint8_t(clamp(1-age/1.8f)*(impact?.84f:.55f)*255),ry);}
}
inline void countdownPaint(lv_layer_t* layer,uint32_t ms) {
    image(layer,&mission_moon,0,350+sceneryOffsetY,205,152,256,309);rect(layer,0,0,410,502,0,153);
    text(layer,"BLOCK FOUND DEMO",160,0xF0F2F5,&inter_24);
    char value[4];snprintf(value,sizeof(value),"%u",unsigned(ms<3000?3-ms/1000:1));
    text(layer,value,286,0xFFFFFF,&inter_96);
    lv_draw_arc_dsc_t d;lv_draw_arc_dsc_init(&d);d.center={205,249};d.radius=75;d.width=3;d.color=lv_color_hex(0xFF982C);
    d.start_angle=270;d.end_angle=270+uint32_t(ms%1000)*360/1000;lv_draw_arc(layer,&d);
}
inline void paint(lv_event_t* event) {
    State* s=static_cast<State*>(lv_event_get_user_data(event));lv_layer_t* layer=lv_event_get_layer(event);
    const uint32_t ms=s->elapsed;
    if(s->countdown){countdownPaint(layer,ms);return;}
    clock_stars::paint(layer);
    image(layer,&mission_moon,0,350+sceneryOffsetY,205,152,256,309);
    using namespace ui::block_found;
    if(!s->fullSequence || ms>=dotsMs) body(layer,4,398,0,1,250);
    else if(ms<touchdownMs) body(layer,0,int(280+118*smooth(ms/float(touchdownMs))),-.025f*(1-clamp(ms/float(touchdownMs))));
    // Once grounded, let the drawn poses carry the motion. Sliding/rotating
    // the whole sprite made planted feet skate and the flag wobble. Keep the
    // ground line and scale fixed through the crouch, windup and impact.
    else if(ms<windupMs) body(layer,1,398,0,1,206);
    else if(ms<impactMs) body(layer,2,398,0,1,206);
    else body(layer,3,398,0,1,250);
    if(s->fullSequence){
        dust(layer,(int64_t(ms)-touchdownMs)/1000.0f);
        const float age=(int64_t(ms)-impactMs)/1000.0f;
        if(age>=0 && age<=1.15f){const float p=ease(age/1.15f);ellipse(layer,250,398+sceneryOffsetY,12+270*p,4+58*p,0xFF931F,uint8_t((1-p)*.9f*255),int(3.5f*(1-p)+.7f));}
        dust(layer,age,250,398,true);
    }
    if(s->fullSequence && ms<impactMs)return;
    const uint32_t beat=beatIndex(ms),color=s->color;
    const int scale=s->fullSequence && beat<4 ? int(256*(1+.04f*expf(-float(beatAgeMs(ms))/75.0f))) : 256;
    const lv_image_dsc_t* words[]={&mission_block,&mission_found};
    for(int word=0;word<2;++word){if(word && s->fullSequence && uint64_t(ms-impactMs)*3<2000)continue;
        const int baseline=word?157:92,w=words[word]->header.w;
        image(layer,words[word],(410-w)/2,baseline-51,w/2,51,scale,scale,0,color);}
}
inline void deleted(lv_event_t* event){State* s=static_cast<State*>(lv_event_get_user_data(event));if(s->timer)lv_timer_delete(s->timer);delete s;}
inline void tick(lv_timer_t* timer){
    State* s=static_cast<State*>(lv_timer_get_user_data(timer));const uint32_t next=s->clock();
    const uint32_t color=s->headingColor?s->headingColor():ui::block_found::headingOrange;
    if(next!=s->elapsed || color!=s->color){s->elapsed=next;s->color=color;lv_obj_invalidate(lv_obj_get_screen(s->root));}
    if(s->fullSequence && !s->countdown && !s->done && next>=ui::block_found::detailsMs){s->done=true;lv_timer_pause(timer);if(s->complete)s->complete();}
}
inline lv_obj_t* create(lv_obj_t* parent,uint32_t (*clock)(),bool fullSequence,void (*complete)()=nullptr,bool countdown=false,uint32_t (*headingColor)()=nullptr){
    State* s=new(std::nothrow)State{};if(!s)return nullptr;
    s->root=lv_obj_create(parent);if(!s->root){delete s;return nullptr;}
    s->clock=clock;s->headingColor=headingColor;s->fullSequence=fullSequence;s->countdown=countdown;s->complete=complete;s->elapsed=fullSequence?0:clock();
    // The opening is created before audio startup; do not start its fallback clock here.
    s->color=!fullSequence && headingColor?headingColor():ui::block_found::headingOrange;
    lv_obj_remove_style_all(s->root);lv_obj_set_size(s->root,410,502);lv_obj_set_pos(s->root,0,0);
    lv_obj_set_style_bg_color(s->root,lv_color_black(),0);lv_obj_set_style_bg_opa(s->root,255,0);
    lv_obj_clear_flag(s->root,static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_event_cb(s->root,paint,LV_EVENT_DRAW_MAIN,s);lv_obj_add_event_cb(s->root,deleted,LV_EVENT_DELETE,s);
    s->timer=lv_timer_create(tick,50,s);if(!s->timer){lv_obj_delete(s->root);return nullptr;}return s->root;
}
} // namespace block_found_scene
