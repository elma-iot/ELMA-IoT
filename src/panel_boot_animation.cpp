#if APP_HAS_ONBOARD_PANEL
#include "panel_boot.h"
#include "panel_boot_layers.h"
namespace {
const lv_img_dsc_t* bootLayer(unsigned index){
    static lv_img_dsc_t images[26]{};
    auto& d=images[index];d.header.cf=LV_IMG_CF_INDEXED_2BIT;d.header.w=128;d.header.h=128;
    d.data_size=sizeof(elmaBootLayers[index]);d.data=elmaBootLayers[index];return &d;
}
int smooth(int t,int begin,int end){if(t<=begin)return 0;if(t>=end)return 255;float x=float(t-begin)/(end-begin);return int(255*x*x*(3-2*x));}
int seeds(int t){const int ends[]={1300,1360,1445,1520,1635,1695,1815,1940,2010,2080,2150};const int levels[]={0,255,15,209,31,255,10,173,41,255,97};for(int i=0;i<11;i++)if(t<ends[i])return levels[i];return 255;}
}
void animatePanelBootLogo(lv_disp_t* display){
    auto* overlay=lv_obj_create(lv_disp_get_layer_top(display));
    lv_obj_set_size(overlay,LV_PCT(100),LV_PCT(100));lv_obj_center(overlay);
    lv_obj_set_style_bg_color(overlay,lv_color_hex(0x111827),0);lv_obj_set_style_bg_opa(overlay,LV_OPA_COVER,0);
    lv_obj_set_style_radius(overlay,0,0);lv_obj_set_style_border_width(overlay,0,0);lv_obj_set_style_pad_all(overlay,0,0);lv_obj_clear_flag(overlay,LV_OBJ_FLAG_SCROLLABLE);
    for(unsigned i=0;i<6;i++){auto* layer=lv_img_create(overlay);lv_img_set_src(layer,bootLayer(i));lv_obj_center(layer);lv_obj_set_style_img_opa(layer,0,0);}
    // Match Android: mark fade, traced swoosh/spark, flickering seeds, text rise, exit fade.
    // Indexed frames remain in flash; LVGL invalidates only the small logo area.
    lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,overlay);lv_anim_set_values(&a,0,3000);lv_anim_set_time(&a,3000);
    lv_anim_set_exec_cb(&a,[](void* p,int32_t t){
        auto* o=static_cast<lv_obj_t*>(p);
        for(unsigned i=0;i<6;i++){
            auto* layer=lv_obj_get_child(o,i);int opacity=i<2?smooth(t,0,600):i==2?seeds(t):i<5?smooth(t,1700,2700):255;
            lv_obj_set_style_img_opa(layer,opacity,0);
            if(i==3||i==4)lv_obj_align(layer,LV_ALIGN_CENTER,0,(255-opacity)*3/255);
            if(i==5)lv_img_set_src(layer,bootLayer(5+smooth(t,500,1800)*20/255));
        }
        lv_obj_set_style_opa(o,255-smooth(t,2700,3000),0);
    });
    lv_anim_set_ready_cb(&a,[](lv_anim_t* a){lv_obj_del(static_cast<lv_obj_t*>(a->var));});lv_anim_start(&a);
}
#endif
