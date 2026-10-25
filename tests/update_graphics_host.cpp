#include "update_graphics.h"
#include "update_screen.h"
#include <vector>
#include <set>
#include <cassert>
#include <fstream>
#include <cmath>
#include <iostream>
int main(){
    for(auto geometry: {std::pair<int,int>{480,480},{320,480},{480,320},{240,240},{135,240},{240,135},{128,64},{64,128},{128,32},{32,128},{96,64},{64,96}}){
        int w=geometry.first,h=geometry.second;UpdateGraphics graphics;
        assert(graphics.begin(w,h,w==480&&h==480));assert(graphics.allocatedBytes()<95000);
        assert(graphics.render(67,100));
        alignas(4) uint16_t row[480]{};graphics.row(row,0);
        // Each DMA callback's length is bytes, its position pixels. Guard the
        // full ten-row bounce buffer and verify it matches per-row rendering.
        std::vector<uint32_t> bounce((w*10*2+3)/4+2,0);bounce.front()=0x12345678;bounce.back()=0x87654321;
        UpdateGraphics::fill(graphics.scanout(),bounce.data()+1,0,w*10*2);
        assert(bounce.front()==0x12345678 && bounce.back()==0x87654321);
        assert(graphics.render(67,300));
        std::vector<uint16_t> pixels(w*h);
        for(int y=0;y<h;++y){
            // Use an aligned temporary row for odd physical widths too.
            alignas(4) uint16_t line[484]{};line[0]=0x4321;line[w+2]=0xabcd;
            graphics.row(line+2,y);assert(line[0]==0x4321 && line[w+2]==0xabcd);
            for(int x=0;x<w;++x)pixels[y*w+x]=line[x+2];
        }
        if(w==480 && h==480){
            assert(pixels[0]==0 && pixels[479]==0);
            const int radius=221;
            for(int degrees:{0,90,180,270}){
                const float a=(degrees-90)*.01745329252f;int x=240+int(cosf(a)*radius),y=240+int(sinf(a)*radius);
                assert(pixels[y*w+x]==(degrees<241?UpdateScreen::blue:0x1106));
            }
            std::set<uint16_t> shades(pixels.begin(),pixels.end());assert(shades.size()>20);
            std::ofstream image(".build-tools/update-screen/round-aa-67.ppm",std::ios::binary);image<<"P6\n480 480\n255\n";
            for(auto p:pixels){char rgb[]={char(((p>>11)&31)*255/31),char(((p>>5)&63)*255/63),char((p&31)*255/31)};image.write(rgb,3);}
            std::cout<<"Round AA renderer internal RAM "<<graphics.allocatedBytes()<<" bytes; shades "<<shades.size()<<"\n";
        }
    }
    std::cout<<"PASS native-resolution graphics, AA, bounded DMA rows and physical controller dimensions\n";
}
