#include "update_graphics.h"
#include "update_font_assets.h"
#include "update_screen.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif
namespace {
void* internal(size_t size){
#if defined(ESP_PLATFORM)
    return heap_caps_calloc(1,size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
#else
    return calloc(1,size);
#endif
}
uint16_t blend(uint16_t a,uint16_t b,unsigned amount,unsigned total=15){
    const unsigned r=(((a>>11)&31)*(total-amount)+((b>>11)&31)*amount+total/2)/total;
    const unsigned g=(((a>>5)&63)*(total-amount)+((b>>5)&63)*amount+total/2)/total;
    const unsigned blue=((a&31)*(total-amount)+(b&31)*amount+total/2)/total;
    return (r<<11)|(g<<5)|blue;
}
struct Span {uint16_t x1=0,n1=0,x2=0,n2=0;uint32_t offset=0;};
struct Frame {uint8_t* text=nullptr;int16_t endX=0,endY=-16384;uint16_t progress=0;uint8_t quadrant=0;};
}
struct UpdateGraphics::State {
    int width,height,textX,textY,textWidth,textHeight,barX,barWidth,barY,barHeight;
    bool round;
    volatile uint32_t pending,scanning;
    Frame frames[2];Span* rows;uint8_t* coverage;
    uint16_t ring[256],white[16];
};
UpdateGraphics::~UpdateGraphics(){
    if(!state_)return;
    for(auto& frame:state_->frames)free(frame.text);
    free(state_->coverage);free(state_->rows);free(state_);
}
bool UpdateGraphics::begin(int width,int height,bool round){
    if(state_||width<32||height<24||width>480||height>480)return false;
    state_=static_cast<State*>(internal(sizeof(State)));if(!state_)return false;
    auto& s=*state_;s.width=width;s.height=height;s.round=round;
    s.textWidth=std::min(width-(width<64?4:8),256);s.textHeight=std::min(height,128);
    s.textX=(width-s.textWidth)/2;s.textY=std::max(0,(height-s.textHeight)/2);
    const size_t textBytes=(s.textWidth*s.textHeight+1)/2;
    bytes_=sizeof(State)+2*textBytes+height*sizeof(Span);
    for(auto& frame:s.frames){frame.text=static_cast<uint8_t*>(internal(textBytes));if(!frame.text)return false;}
    s.rows=static_cast<Span*>(internal(height*sizeof(Span)));if(!s.rows)return false;
    const float outer=std::min(width,height)/2.f-10.f;
    const float thickness=std::max(4.f,std::min(width,height)*18.f/480.f),inner=outer-thickness;
    s.barX=std::min(24,width/8);s.barWidth=width-2*s.barX;s.barHeight=height<48?4:std::max(6,height/40);
    s.barY=height<96?height-s.barHeight-3:std::min(height-s.barHeight-12,height/2+(width>=320?62:48));
    uint32_t count=0;
    for(int y=0;y<height;++y){
        auto& row=s.rows[y];row.offset=count;
        if(round){
            const float dy=y+.5f-height/2.f;
            const float square=(outer+.5f)*(outer+.5f)-dy*dy;if(square<=0)continue;
            const float extent=sqrtf(square);
            const int left=std::max(0,int(floorf(width/2.f-extent-.5f))),right=std::min(width-1,int(ceilf(width/2.f+extent-.5f)));
            const float hole=(inner-.5f)*(inner-.5f)-dy*dy;
            if(hole>0){
                const float inset=sqrtf(hole);
                const int firstEnd=std::min(right,int(ceilf(width/2.f-inset-.5f))),secondStart=std::max(firstEnd+1,int(floorf(width/2.f+inset-.5f)));
                row.x1=left;row.n1=std::max(0,firstEnd-left+1);row.x2=secondStart;row.n2=std::max(0,right-secondStart+1);
            }else{row.x1=left;row.n1=right-left+1;}
        }else if(y>=s.barY&&y<s.barY+s.barHeight){row.x1=s.barX;row.n1=s.barWidth;}
        count+=row.n1+row.n2;
    }
    s.coverage=static_cast<uint8_t*>(internal((count+1)/2));if(!s.coverage)return false;bytes_+=(count+1)/2;
    for(int y=0;y<height;++y){
        const auto& row=s.rows[y];unsigned offset=row.offset;
        for(int segment=0;segment<2;++segment){const int start=segment?row.x2:row.x1,length=segment?row.n2:row.n1;
            for(int x=start;x<start+length;++x,++offset){
                float opacity=1.f;
                if(round){const float dx=x+.5f-width/2.f,dy=y+.5f-height/2.f,d=sqrtf(dx*dx+dy*dy);opacity=std::min(outer+.5f-d,d-(inner-.5f));}
                else{const float r=s.barHeight/2.f,cy=s.barY+r,cx=x<s.barX+r?s.barX+r:x>s.barX+s.barWidth-r?s.barX+s.barWidth-r:x+.5f;const float dx=x+.5f-cx,dy=y+.5f-cy;opacity=r+.5f-sqrtf(dx*dx+dy*dy);}
                const unsigned alpha=unsigned(std::max(0.f,std::min(1.f,opacity))*15.f+.5f);
                s.coverage[offset/2]|=alpha<<((offset&1)?0:4);
            }
        }
    }
    for(unsigned radial=0;radial<16;++radial){
        s.white[radial]=blend(0,0xffff,radial);
        for(unsigned angle=0;angle<16;++angle)s.ring[radial*16+angle]=blend(0,blend(0x1106,UpdateScreen::blue,angle),radial);
    }
    return true;
}
bool UpdateGraphics::render(unsigned value,uint32_t now){
    if(!state_)return false;value=UpdateScreen::percent(value);
    const unsigned dots=UpdateScreen::dots(now);
    if(value!=percent_){from_=displayed_;target_=value*100;transitionAt_=now;percent_=value;}
    const unsigned elapsed=std::min(uint32_t(now-transitionAt_),uint32_t(150));
    const int interpolated=from_+(target_-from_)*int(elapsed)/150;
    if(now-lastDraw_<33 && lastDraw_)return false;
    if(interpolated==displayed_&&dots==dots_&&lastDraw_)return false;
    auto& s=*state_;const uint32_t page=s.pending^1U;if(page==s.scanning)return false;
    auto& frame=s.frames[page];memset(frame.text,0,(s.textWidth*s.textHeight+1)/2);
    const auto& titleFont=s.width>=320&&s.height>=200?UpdateFonts::font28:s.width>=200&&s.height>=160?UpdateFonts::font20:s.width<48?UpdateFonts::font8:s.width<96?UpdateFonts::font10:UpdateFonts::font14;
    const auto& valueFont=s.width>=320&&s.height>=200?UpdateFonts::font48:s.width>=160&&s.height>=100?UpdateFonts::font28:s.width<48?UpdateFonts::font8:s.width<96?UpdateFonts::font14:UpdateFonts::font20;
    auto width=[](const UpdateFonts::Font& font,const char* text){unsigned advance=0;for(;*text;++text)if(auto* glyph=UpdateFonts::glyph(font,*text))advance+=glyph->advance;return int((advance+8)/16);};
    auto draw=[&](const UpdateFonts::Font& font,const char* text,int left,int top){
        int pen=left*16;
        for(;*text;++text){auto* glyph=UpdateFonts::glyph(font,*text);if(!glyph)continue;
            const int x0=(pen+8)/16+glyph->x,y0=top+font.height-font.baseline-glyph->height-glyph->y;
            for(unsigned gy=0;gy<glyph->height;++gy)for(unsigned gx=0;gx<glyph->width;++gx){
                const int x=x0+gx-s.textX,y=y0+gy-s.textY;if(x<0||y<0||x>=s.textWidth||y>=s.textHeight)continue;
                const unsigned source=gy*glyph->width+gx;const uint8_t alpha=(font.bitmap[glyph->offset+source/2]>>((source&1)?0:4))&15;
                const unsigned target=y*s.textWidth+x;uint8_t& pixel=frame.text[target/2];const unsigned shift=(target&1)?0:4;
                if(alpha>((pixel>>shift)&15))pixel=(pixel&~(15<<shift))|(alpha<<shift);
            }
            pen+=glyph->advance;
        }
    };
    char label[8];snprintf(label,sizeof(label),"%u%%",value);
    const char* word=s.width<48?"Update":"Updating";
    const int wordWidth=width(titleFont,word),titleTop=s.height<96?3:s.height/2-(s.width>=320?44:s.width<48?16:32);
    if(s.height>=48){
        draw(titleFont,word,(s.width-wordWidth)/2,titleTop);
        char marks[4]="...";marks[dots]=0;if(s.width>=96)draw(titleFont,marks,(s.width+wordWidth)/2+3,titleTop);
        draw(valueFont,label,(s.width-width(valueFont,label))/2,s.height<96?25:s.height/2+2);
    }else{
        const int left=(s.width-wordWidth-width(titleFont,label)-6)/2;
        draw(titleFont,word,left,1);draw(titleFont,label,left+wordWidth+6,1);
    }
    frame.progress=std::max(0,std::min(10000,interpolated));frame.quadrant=frame.progress/2500;
    const float radians=frame.progress*0.0006283185307f;frame.endX=int16_t(sinf(radians)*16384);frame.endY=int16_t(-cosf(radians)*16384);
#if defined(_MSC_VER)
    s.pending=page;
#else
    __atomic_store_n(&s.pending,page,__ATOMIC_RELEASE);
#endif
    lastDraw_=now;dots_=dots;displayed_=interpolated;return true;
}
void UpdateGraphics::row(uint16_t* output,int y){if(state_)fill(state_,output,y*state_->width,state_->width*2);}
void IRAM_ATTR UpdateGraphics::fill(void* context,void* output,int position,int length){
    auto* s=static_cast<State*>(context);if(!s||!output)return;
    if(position==0){
#if defined(_MSC_VER)
        s->scanning=s->pending;
#else
        s->scanning=__atomic_load_n(&s->pending,__ATOMIC_ACQUIRE);
#endif
    }
    const Frame* frame=&s->frames[s->scanning];
    auto* clear=static_cast<volatile uint32_t*>(output);for(int n=0;n<length/4;++n)clear[n]=0;
    auto* pixels=static_cast<uint16_t*>(output);if(length&2)pixels[length/2-1]=0;
    const int end=position+length/2;
    for(int at=position;at<end;){
        const int y=at/s->width,x=at%s->width,limit=(end-at<s->width-x)?x+end-at:s->width;
        if(y>=s->height)break;const Span* row=&s->rows[y];uint16_t* destination=pixels+(at-position);
        for(int segment=0;segment<2;++segment){
            const int start=segment?row->x2:row->x1,count=segment?row->n2:row->n1;
            const int left=start>x?start:x,right=start+count<limit?start+count:limit;
            for(int px=left;px<right;++px){
                const unsigned offset=row->offset+(segment?row->n1:0)+px-start;
                const unsigned radial=(s->coverage[offset/2]>>((offset&1)?0:4))&15;
                int angle=0;
                if(frame->progress>=10000)angle=15;
                else if(frame->progress){
                    if(s->round){
                        const int dx=2*px+1-s->width,dy=2*y+1-s->height;
                        const int quadrant=dy<0?(dx>=0?0:3):(dx>=0?1:2);
                        if(quadrant<frame->quadrant)angle=15;
                        else if(quadrant==frame->quadrant){const int cross=frame->endX*dy-frame->endY*dx;angle=cross<=-16384?15:cross>=16384?0:((16384-cross)*15)>>15;}
                    }else{const int distance=(s->barX*10000+s->barWidth*frame->progress)-(px*10000+5000);angle=distance<=-5000?0:distance>=5000?15:(distance+5000)*15/10000;}
                }
                destination[px-x]=s->ring[radial*16+angle];
            }
        }
        if(y>=s->textY&&y<s->textY+s->textHeight){
            const int left=s->textX>x?s->textX:x,right=s->textX+s->textWidth<limit?s->textX+s->textWidth:limit;
            for(int px=left;px<right;++px){const unsigned n=(y-s->textY)*s->textWidth+px-s->textX;const unsigned alpha=(frame->text[n/2]>>((n&1)?0:4))&15;if(alpha)destination[px-x]=s->white[alpha];}
        }
        at+=limit-x;
    }
}
