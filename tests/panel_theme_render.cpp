#include "panel_theme.h"
#include "panel_boot.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>

static int width=320,height=480;
static std::vector<unsigned char> image;
static void flush(lv_disp_drv_t* driver,const lv_area_t* area,lv_color_t* pixels){
    for(int y=area->y1;y<=area->y2;y++)for(int x=area->x1;x<=area->x2;x++){
        auto color=lv_color_to32(*pixels++);auto at=(y*width+x)*3;
        image[at]=(color>>16)&255;image[at+1]=(color>>8)&255;image[at+2]=color&255;
    }
    lv_disp_flush_ready(driver);
}
static void save(const char* name){
    lv_refr_now(nullptr);FILE* file=fopen(name,"wb");assert(file);
    fprintf(file,"P6\n%d %d\n255\n",width,height);fwrite(image.data(),1,image.size(),file);fclose(file);
}
static void readable(lv_obj_t* object,lv_part_t part){
    auto fg=lv_color_to32(lv_obj_get_style_text_color(object,part));
    auto bg=lv_color_to32(lv_obj_get_style_bg_color(object,part));
    auto luminance=[](unsigned color){auto channel=[](unsigned c){double v=c/255.0;return v<=0.04045?v/12.92:pow((v+0.055)/1.055,2.4);};return 0.2126*channel((color>>16)&255)+0.7152*channel((color>>8)&255)+0.0722*channel(color&255);};
    double a=luminance(fg),b=luminance(bg);double ratio=(std::max(a,b)+0.05)/(std::min(a,b)+0.05);
    printf("Text contrast: %.2f:1\n",ratio);assert(ratio>=4.5);
}
int main(){
    lv_init();static lv_color_t pixels[320*24];lv_disp_draw_buf_t buffer;lv_disp_draw_buf_init(&buffer,pixels,nullptr,320*24);
    lv_disp_drv_t driver;lv_disp_drv_init(&driver);driver.draw_buf=&buffer;driver.flush_cb=flush;driver.hor_res=width;driver.ver_res=height;
    auto* display=lv_disp_drv_register(&driver);lv_disp_set_theme(display,elmaPanelTheme(display));
    for(int size=0;size<2;size++){
        width=size?240:320;height=size?320:480;driver.hor_res=width;driver.ver_res=height;lv_disp_drv_update(display,&driver);image.resize(width*height*3);
        auto* screen=lv_scr_act();lv_obj_clean(screen);lv_obj_set_style_bg_color(screen,lv_color_hex(0x111827),0);lv_obj_set_style_pad_all(screen,0,0);lv_obj_set_style_border_width(screen,0,0);
        auto* menu=lv_dropdown_create(screen);lv_obj_set_pos(menu,8,8);lv_obj_set_size(menu,width-16,44);
        lv_dropdown_set_options(menu,"Configuration\nAudio\nWi-Fi\nMQTT\nHardware Monitor\nExternal Storage\nInfo");
        readable(menu,LV_PART_MAIN);assert(lv_obj_get_style_border_width(menu,LV_PART_MAIN)==1);
        auto* frame=lv_obj_create(screen);lv_obj_set_pos(frame,8,64);lv_obj_set_size(frame,width-16,height-72);
        auto* title=lv_label_create(frame);lv_label_set_text(title,"Wi-Fi settings");
        auto* input=lv_textarea_create(frame);lv_obj_set_pos(input,0,28);lv_obj_set_size(input,width-36,44);lv_textarea_set_one_line(input,true);lv_textarea_set_text(input,"Saved network");readable(input,LV_PART_MAIN);
        auto* button=lv_btn_create(frame);lv_obj_set_pos(button,0,84);lv_obj_set_size(button,width-36,44);auto* caption=lv_label_create(button);lv_label_set_text(caption,"Scan networks");lv_obj_center(caption);readable(button,LV_PART_MAIN);
        auto* bar=lv_bar_create(frame);lv_obj_set_pos(bar,0,142);lv_obj_set_size(bar,width-36,16);lv_bar_set_value(bar,65,LV_ANIM_OFF);
        assert(lv_obj_get_style_border_width(frame,LV_PART_MAIN)==1);
        save(size?"panel-controls-240.ppm":"panel-controls-320.ppm");
        lv_dropdown_open(menu);auto* list=lv_dropdown_get_list(menu);assert(list);assert(lv_obj_get_style_text_font(list,LV_PART_MAIN)->line_height+lv_obj_get_style_text_line_space(list,LV_PART_MAIN)>=40);readable(list,LV_PART_MAIN);readable(list,LV_PART_SELECTED);assert(lv_obj_get_style_border_width(list,LV_PART_MAIN)==1);
        lv_obj_add_state(list,LV_STATE_CHECKED);readable(list,LV_PART_SELECTED);
        save(size?"panel-menu-240.ppm":"panel-menu-320.ppm");lv_dropdown_close(menu);
        lv_obj_t* format=nullptr;auto* dialog=elmaSdFormatDialog(display,&format);lv_obj_update_layout(dialog);
        lv_area_t bounds;lv_obj_get_coords(dialog,&bounds);printf("Dialog %d: %d,%d-%d,%d\n",width,bounds.x1,bounds.y1,bounds.x2,bounds.y2);
        assert(bounds.x1>=0&&bounds.y1>=0&&bounds.x2<width&&bounds.y2<height);
        assert(std::string(lv_dropdown_get_options(format))=="FAT32");readable(dialog,LV_PART_MAIN);
        auto* buttons=lv_msgbox_get_btns(dialog);assert(std::string(lv_btnmatrix_get_btn_text(buttons,0))=="Cancel");assert(std::string(lv_btnmatrix_get_btn_text(buttons,1))=="Erase and format");
        save(size?"panel-confirm-240.ppm":"panel-confirm-320.ppm");lv_msgbox_close(dialog);
        auto before=lv_obj_get_child_cnt(lv_layer_top());animatePanelBootLogo(display);
        std::vector<unsigned char> previous;
        for(int step=0;step<7;step++){
            lv_tick_inc(450);lv_timer_handler();char name[80];snprintf(name,sizeof(name),"panel-boot-%d-%d.ppm",width,step);save(name);
            if(step>0&&step<6)assert(image!=previous);previous=image;
        }
        assert(lv_obj_get_child_cnt(lv_layer_top())==before);
        auto* keyboard=lv_keyboard_create(screen);readable(keyboard,LV_PART_ITEMS);lv_obj_add_state(keyboard,LV_STATE_PRESSED);readable(keyboard,LV_PART_ITEMS);
    }
    puts("LCD theme: 240/320 px controls, frames, popup selections and keyboard contrast passed");
}
