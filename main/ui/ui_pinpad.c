/* SPDX-License-Identifier: Apache-2.0
 * 中文：独立密码键盘；导入系统字形不拉伸，原始锁屏背景和液态玻璃圆键分开绘制。
 * 用户修订：数字圆键改成白层 25% 的液态玻璃，浮在原始画面（背景不做高斯模糊、不加白底）上；按下只加深内圈、抬手用按下前底图恢复。
 * English: Dedicated PIN pad; unscaled system glyphs, sharp lock backdrop and liquid-glass keys painted separately.
 * User revision: the number keys are liquid glass with a 25% white layer over the sharp lock frame (no
 * backdrop blur or white ground); pressing only inks the inner ring and release restores the backdrop
 * saved on press.
 * 参考：MokuMMk/MokuReader 1d7951ec 的液态玻璃圆键；4bpp缓存与输入恢复为本机适配。
 * Reference: MokuMMk/MokuReader 1d7951ec glass keys, adapted with 4bpp snapshots and input restoration.
 * 冻结：不切字体、不写凭据、不调用显示；缓存和输入在退出时释放/清零。
 * Frozen: No face switches, credentials or presentation; release cache and wipe input on exit.
 */
#include "ui_pinpad.h"
#include "ui_kit.h"
#include "ui_glass.h"
#include "ttf_font.h"
#include "ui_font.h"
#include "lock_pin.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define PAD_W 684
#define PAD_H 1216
#define RADIUS 72
// 按下键的底图边长（含出边 3px）。/ Side of the pressed-key backdrop, including the 3 px overhang.
#define KEY_SNAP_SIDE (2*RADIUS+6)
#define KEY_SNAP_BYTES (((size_t)KEY_SNAP_SIDE*KEY_SNAP_SIDE+1)/2)
static const int xs[3]={158,342,526};
static const int ys[4]={544,703,862,1021};
// 横版键盘：1216×684 画布，数字区在右、说明与按钮在左；圆键更小、按键底图仍复用同一张快照。
// Landscape pad on the 1216×684 canvas: digits on the right, copy and buttons on the left; the keys
// are smaller but still reuse the same pressed-key snapshot buffer.
static int pad_w(const ui_pinpad_t *pad){return pad&&pad->landscape?epd_width():PAD_W;}
static int pad_h(const ui_pinpad_t *pad){return pad&&pad->landscape?epd_height():PAD_H;}

// 横版说明区：锁图标 + 标题 + 四个圆点 + 提示，全部在左半边。
// Landscape copy block: lock icon, title, four dots and the notice, all on the left half.
#define L_BLOCK_X 300
#define L_DOTS_Y 310
EpdRect ui_pinpad_full(void){return (EpdRect){0,0,epd_rotated_display_width(),epd_rotated_display_height()};}
EpdRect ui_pinpad_entry_area(void){
    // 方向查询写在本函数里：宿主测试按函数切片，借外部小助手会少一个定义。
    // The orientation check lives inside this function: host tests slice functions, and borrowing a
    // helper would leave that definition out of the unit.
    enum EpdRotation rotation=epd_get_rotation();
    bool landscape=rotation==EPD_ROT_LANDSCAPE||rotation==EPD_ROT_INVERTED_LANDSCAPE;
    return landscape?(EpdRect){140,270,330,120}:(EpdRect){196,371,292,91};
}

static uint8_t pixel(const uint8_t *frame,int x,int y){
    int px=x,py=y;
    switch(epd_get_rotation()){
        case EPD_ROT_PORTRAIT:px=epd_width()-y-1;py=x;break;
        case EPD_ROT_INVERTED_LANDSCAPE:px=epd_width()-x-1;py=epd_height()-y-1;break;
        case EPD_ROT_INVERTED_PORTRAIT:px=y;py=epd_height()-x-1;break;
        default:break;
    }
    return epd_get_pixel(px,py,epd_width(),epd_height(),frame);
}
void ui_pinpad_end(ui_pinpad_t *pad){
    if(!pad)return;
    free(pad->background);free(pad->key_snapshot);pad->background=NULL;pad->key_snapshot=NULL;
    lock_pin_wipe(pad,sizeof(*pad));pad->pressed=-1;pad->snapshot_hit=-1;
}
void ui_pinpad_reset(ui_pinpad_t *pad,const char *title,const char *notice){
    lock_pin_wipe(pad->digits,sizeof(pad->digits));pad->count=0;pad->pressed=-1;pad->snapshot_hit=-1;pad->dirty_key=-1;pad->blocked=false;
    if(title)snprintf(pad->title,sizeof(pad->title),"%s",title);
    snprintf(pad->notice,sizeof(pad->notice),"%s",notice?notice:"");
}
void ui_pinpad_begin(ui_pinpad_t *pad,const uint8_t *frame,const char *title){
    ui_pinpad_end(pad);ui_pinpad_reset(pad,title,"");
    // 键盘方向在进入时定死：锁屏期间旋转不会变，中途改方向只会让已画好的底图和命中错位。
    // The pad orientation is fixed on entry: the rotation cannot change while locked, and flipping it
    // midway would desync the painted backdrop and the hit regions.
    pad->landscape=ui_canvas_landscape();
    // 背景保留原始锁屏画面、不做高斯模糊也不加白底；模糊和白层都只发生在圆键自己的液态玻璃里。
    // The backdrop keeps the original lock frame with neither a Gaussian blur nor a white ground;
    // both the blur and the white layer live only inside the liquid-glass keys.
    size_t need=PAD_W*PAD_H/2u+KEY_SNAP_BYTES;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<need+1024u*1024u)return;
    // 按键底图只用于按下/抬手反馈；分配不到时玻璃键照画，只是没有按下内圈。
    // The key backdrop only drives press/release feedback; without it the glass keys still paint,
    // just without the pressed inner ring.
    pad->key_snapshot=heap_caps_malloc(KEY_SNAP_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    pad->background=heap_caps_malloc(PAD_W*PAD_H/2u,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!pad->background){free(pad->key_snapshot);pad->key_snapshot=NULL;pad->background=NULL;return;}
    if(frame)memcpy(pad->background,frame,PAD_W*PAD_H/2u);
    else memset(pad->background,0xee,PAD_W*PAD_H/2u);
}
static bool intersects(EpdRect a,EpdRect b){return a.x<b.x+b.width&&b.x<a.x+a.width&&a.y<b.y+b.height&&b.y<a.y+a.height;}
static EpdRect key_rect(const ui_pinpad_t *pad,int hit){
    if(pad->landscape){
        // 横版：取消/删除移进左半边说明区，数字区仍按 3×4 排（0 在末行中间）。
        // 几何放在函数内，宿主测试按函数切片编译时不会丢掉这些常量。
        // Landscape: cancel/delete move into the left copy block; the digits keep the 3×4 grid with 0
        // in the middle of the last row. The geometry lives inside the function so slicing this
        // function into a host test keeps it.
        static const int lx[3]={720,900,1080},ly[4]={112,252,392,532};
        const int radius=56;
        if(hit<10){int col=hit?((hit-1)%3):1,row=hit?((hit-1)/3):3;return(EpdRect){lx[col]-radius-3,ly[row]-radius-3,2*radius+6,2*radius+6};}
        return(EpdRect){hit==10?120:320,470,160,64};
    }
    if(hit<10){int col=hit?((hit-1)%3):1,row=hit?((hit-1)/3):3;return(EpdRect){xs[col]-RADIUS-3,(pad->navigation?532+148*row:ys[row])-RADIUS-3,2*RADIUS+6,2*RADIUS+6};}
    return(EpdRect){hit==10?74:470,pad->navigation?1012:1100,140,68};
}
static EpdRect joined(EpdRect a,EpdRect b){int x=a.x<b.x?a.x:b.x,y=a.y<b.y?a.y:b.y,r=a.x+a.width>b.x+b.width?a.x+a.width:b.x+b.width,t=a.y+a.height>b.y+b.height?a.y+a.height:b.y+b.height;return(EpdRect){x,y,r-x,t-y};}
// 按下前把整枚玻璃键（含数字）存成底图，抬手原样贴回，避免重画字形或留残影。
// Save the whole glass key including its digit before a press; release pastes it back so no glyph
// is redrawn and no residue is left.
static void key_snapshot_save(uint8_t *fb,ui_pinpad_t *pad,int hit){
    if(!pad->key_snapshot||pad->snapshot_hit==hit)return;
    EpdRect box=key_rect(pad,hit);
    for(int y=0;y<box.height;++y)for(int x=0;x<box.width;++x){
        size_t at=(size_t)y*box.width+x;
        uint8_t value=(uint8_t)(pixel(fb,box.x+x,box.y+y)>>4);
        if(at&1)pad->key_snapshot[at/2]|=(uint8_t)(value<<4);
        else pad->key_snapshot[at/2]=value;
    }
    pad->snapshot_hit=hit;
}
static void key_snapshot_restore(uint8_t *fb,ui_pinpad_t *pad,int hit){
    if(!pad->key_snapshot||pad->snapshot_hit!=hit)return;
    EpdRect box=key_rect(pad,hit);
    for(int y=0;y<box.height;++y)for(int x=0;x<box.width;++x){
        size_t at=(size_t)y*box.width+x;
        epd_draw_pixel(box.x+x,box.y+y,(uint8_t)(((pad->key_snapshot[at/2]>>((at&1)*4))&15)<<4),fb);
    }
    pad->snapshot_hit=-1;
}
static void fallback_text(uint8_t *fb,int cx,int cy,int em,const char *value,uint8_t color){
    int px=ui_font_title_px(em,value),a=0,b=0;ui_font_measure_line_px(px,value,&a,&b);
    // 内建接口接受灰阶编号0..15；用当前背景保持按下态，不覆盖一块浅色底。
    // The built-in API takes levels 0..15; retain the pressed backdrop without a light patch.
    ui_font_draw_text_px(fb,cx,cy+(a-b)/2,px,value,EPD_DRAW_ALIGN_CENTER,color>>4,pixel(fb,cx,cy)>>4,false);
}
static void text(uint8_t *fb,int cx,int cy,int em,const char *value,uint8_t color,const ui_pinpad_t *pad,int key){
    (void)key;
    const int W=pad_w(pad),H=pad_h(pad);
    if(!ttf_font_ready()||!ttf_font_has_text(value)){fallback_text(fb,cx,cy,em,value,color);return;}
    int px=ttf_em_height_px(em),a=0,b=0;ttf_measure_line_px(px,value,&a,&b);
    int w=ttf_text_width_px(px,value)+4,h=a+b+4;
    if(w<=0||w>W||h<=0||h>180){fallback_text(fb,cx,cy,em,value,color);return;}
    uint8_t *mask=heap_caps_calloc(1,(size_t)w*h,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!mask){fallback_text(fb,cx,cy,em,value,color);return;}
    if(ttf_text_mask_px(mask,w,h,2,a+2,px,value))for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        unsigned alpha=mask[y*w+x];int xx=cx-w/2+x,yy=cy-(a+b)/2-2+y;
        if(alpha&&xx>=0&&xx<W&&yy>=0&&yy<H){unsigned bg=pixel(fb,xx,yy);epd_draw_pixel(xx,yy,(color*alpha+bg*(255-alpha)+127)/255,fb);}
    }
    free(mask);
}
static void paint(uint8_t *fb,const ui_pinpad_t *pad,EpdRect area){
    const int W=pad_w(pad),H=pad_h(pad);
    const int radius=(key_rect(pad,0).width-6)/2;
    int right=area.x+area.width,bottom=area.y+area.height;
    for(int y=area.y;y<bottom&&y<H;++y)for(int x=area.x;x<right&&x<W;++x)
        if(x>=0&&y>=0){
            uint8_t gray=pad->background?pixel(pad->background,x,y):0xe0;
            epd_draw_pixel(x,y,gray,fb);
        }
    if(pad->landscape){
        // 横版说明区在左半边：图标、标题、四个圆点、提示；按钮由下面的键循环画。
        // The landscape copy block sits on the left half: icon, title, four dots and the notice; the
        // buttons are painted by the key loop below.
        if(intersects(area,(EpdRect){L_BLOCK_X-90,90,180,180})){
            ui_draw_icon(fb,L_BLOCK_X,140,56,UI_ICON_LOCK,0);
            text(fb,L_BLOCK_X,236,44,pad->title,0,pad,-1);
        }
        if(intersects(area,ui_pinpad_entry_area())){
            unsigned count=pad->count+(pad->pressed>=0&&pad->pressed<10&&pad->count<4);
            for(int i=0;i<4;++i){int x=200+i*67;epd_draw_circle(x,L_DOTS_Y,14,0,fb);epd_draw_circle(x,L_DOTS_Y,13,0,fb);epd_fill_circle(x,L_DOTS_Y,12,(unsigned)i<count?0:255,fb);}
            if(pad->notice[0])text(fb,L_BLOCK_X,L_DOTS_Y+50,20,pad->notice,0,pad,-1);
        }
    } else {
        if(intersects(area,(EpdRect){180,135,324,168})){
            // 锁用图标库里的那一枚：全机图标同源，换起来也只改这里。
            // The lock comes from the icon set, the single source for every icon in the firmware.
            ui_draw_icon(fb,342,165,56,UI_ICON_LOCK,0);
            text(fb,342,260,48,pad->title,0,pad,-1);
        }
        if(intersects(area,ui_pinpad_entry_area())){
            unsigned count=pad->count+(pad->pressed>=0&&pad->pressed<10&&pad->count<4);
            for(int i=0;i<4;++i){int x=241+i*67;epd_draw_circle(x,401,14,0,fb);epd_draw_circle(x,401,13,0,fb);epd_fill_circle(x,401,12,(unsigned)i<count?0:255,fb);}
            if(pad->notice[0])text(fb,342,445,20,pad->notice,0,pad,-1);
        }
    }
    for(int hit=0;hit<12;++hit){EpdRect box=key_rect(pad,hit);if(!intersects(area,box))continue;bool pressed=pad->pressed==hit;
        if(hit<10){int cx=box.x+radius+3,cy=box.y+radius+3;
            // 数字圆键是无白底背景上的液态玻璃：模糊、折射和亮边都只发生在键内，白层 25%；
            // 折射强度参考长按电源页的 160%，边缘形变更明显。
            // Number keys are liquid glass over the un-whitened frame: blur, refraction and rim stay
            // inside the key, with a 25% white layer; refraction strength matches the power page's
            // 160% so the rim deforms more.
            if(!ui_glass_panel(fb,(EpdRect){cx-radius,cy-radius,2*radius,2*radius},radius,25,160)){
                epd_fill_circle(cx,cy,radius,255,fb);
                for(int r=radius;r>=radius-2;--r)epd_draw_circle(cx,cy,r,0x60,fb);
            }
            char numeral[2]={(char)('0'+hit),0};text(fb,cx,cy,pad->landscape?58:74,numeral,0,pad,hit);
            if(pressed&&pad->key_snapshot){
                for(int r=radius-5;r>=radius-9;--r)epd_draw_circle(cx,cy,r,0,fb);
            }
        }else text(fb,box.x+box.width/2,box.y+32,27,hit==10?"取消":"删除",0,pad,hit);
    }
}
void ui_pinpad_paint(uint8_t *fb,const ui_pinpad_t *pad,EpdRect area){paint(fb,pad,area);}

void ui_pinpad_paint_input(uint8_t *fb,ui_pinpad_t *pad){
    // 不生成TTF蒙版、不分配内存：按下只存底图并加深内圈，抬手把按下前像素贴回。
    // No glyph masks or allocations: a press saves the backdrop and inks the inner ring, and release
    // pastes the pre-press pixels back.
    int hit=pad->dirty_key;
    const int radius=(key_rect(pad,0).width-6)/2;
    if(hit>=0&&hit<10){
        EpdRect box=key_rect(pad,hit);int cx=box.x+radius+3,cy=box.y+radius+3;
        if(pad->pressed==hit){
            if(pad->key_snapshot){
                key_snapshot_save(fb,pad,hit);
                for(int r=radius-5;r>=radius-9;--r)epd_draw_circle(cx,cy,r,0,fb);
            }
        }else key_snapshot_restore(fb,pad,hit);
    }else if(hit>=10){
        EpdRect box=key_rect(pad,hit);
        for(int y=box.y+54;y<box.y+56;++y)for(int x=box.x+32;x<box.x+108;++x)
            epd_draw_pixel(x,y,pad->pressed==hit?0:pad->background?pixel(pad->background,x,y):0xe0,fb);
    }
    unsigned count=pad->count+(pad->pressed>=0&&pad->pressed<10&&pad->count<4);
    if(pad->landscape){
        for(int i=0;i<4;++i)epd_fill_circle(200+i*67,L_DOTS_Y,12,(unsigned)i<count?0:255,fb);
    } else {
        for(int i=0;i<4;++i)epd_fill_circle(241+i*67,401,12,(unsigned)i<count?0:255,fb);
    }
}
static int hit_test(const ui_pinpad_t *pad,int x,int y){
    const int radius=(key_rect(pad,0).width-6)/2;
    if(x<0||x>=pad_w(pad)||y<0||y>=pad_h(pad))return -1;
    for(int i=0;i<12;++i){EpdRect box=key_rect(pad,i);if(i<10){int dx=x-box.x-radius-3,dy=y-box.y-radius-3;if(dx*dx+dy*dy<=radius*radius)return i;}else if(x>=box.x&&x<box.x+box.width&&y>=box.y&&y<box.y+box.height)return i;}
    return -1;
}
ui_pin_result_t ui_pinpad_handle(ui_pinpad_t *pad,const ui_gesture_event_t *event,EpdRect *dirty){
    // 触摸报点与竖屏逻辑一致；横版键盘在 1216×684 画布上，所以先把报点换算到横屏逻辑坐标，
    // 公式与阅读页的 reader_touch_to_landscape 同一套（两种横屏方向各一个）。
    // Touch reports portrait-logical coordinates while the landscape pad lives on the 1216×684 canvas,
    // so map the points first with the same formulas the reader uses (one per landscape rotation).
    ui_gesture_event_t mapped=*event;
    if(pad->landscape){
        uint16_t x0=mapped.x0,y0=mapped.y0,x1=mapped.x,y1=mapped.y;
        if(epd_get_rotation()==EPD_ROT_LANDSCAPE){
            mapped.x0=y0;mapped.y0=(uint16_t)(epd_height()-1-x0);
            mapped.x=y1;mapped.y=(uint16_t)(epd_height()-1-x1);
        }else{
            mapped.x0=(uint16_t)(epd_width()-1-y0);mapped.y0=x0;
            mapped.x=(uint16_t)(epd_width()-1-y1);mapped.y=x1;
        }
    }
    const ui_gesture_event_t *ev=&mapped;
    *dirty=ui_pinpad_entry_area();pad->dirty_key=-1;int prior=pad->pressed;
    if(ev->type==UI_GESTURE_PRESS){
        int hit=hit_test(pad,ev->x0,ev->y0);if(hit<0||((pad->blocked||pad->count==4)&&hit<10))return UI_PIN_NONE;
        pad->pressed=hit;pad->dirty_key=hit;*dirty=joined(*dirty,key_rect(pad,hit));return UI_PIN_CHANGED;
    }
    if(prior<0)return UI_PIN_NONE;
    if(ev->type==UI_GESTURE_MOVE&&hit_test(pad,ev->x,ev->y)==prior)return UI_PIN_NONE;
    pad->pressed=-1;pad->dirty_key=prior;*dirty=joined(*dirty,key_rect(pad,prior));
    if(ev->type!=UI_GESTURE_TAP||hit_test(pad,ev->x,ev->y)!=prior||hit_test(pad,ev->x0,ev->y0)!=prior)return UI_PIN_CHANGED;
    if(prior==10)return UI_PIN_CANCEL;
    if(prior==11){if(pad->count)pad->digits[--pad->count]=0;return UI_PIN_CHANGED;}
    if(pad->count<4){pad->digits[pad->count++]=(char)('0'+prior);pad->digits[pad->count]=0;}
    return pad->count==4?UI_PIN_COMPLETE:UI_PIN_CHANGED;
}
