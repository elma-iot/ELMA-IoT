#include "rotary_hmi.h"
#if APP_ROTARY_HMI
#include "panel_display.h"
#include "panel_memory.h"
#include "panel_theme.h"
#include "device_log.h"
#include "round_system_dashboard.h"
#include <esp_system.h>
#include <lvgl.h>
#include <set>
#include <functional>
#include <cmath>
#include <ctime>
#include <WiFi.h>
#include <freertos/semphr.h>
namespace RoundHmi {
namespace {
struct Guard {
    static SemaphoreHandle_t mutex(){static SemaphoreHandle_t handle=xSemaphoreCreateRecursiveMutex();return handle;}
    bool locked;Guard():locked(mutex()&&xSemaphoreTakeRecursive(mutex(),portMAX_DELAY)==pdTRUE){}
    ~Guard(){if(locked)xSemaphoreGiveRecursive(mutex());}
    explicit operator bool() const{return locked;}
};
PanelDisplay* panel=nullptr;RotaryHmi::Model model;RotaryHmi::Encoder decoder;
RoundSystemDashboard dashboard;
TaskHandle_t failsafeTask=nullptr;
void buttonFailsafe(void*){
    RotaryHmi::HoldFailsafe hold;
    for (;;) {
        if (hold.sample(digitalRead(0) == LOW, millis())) {
            const char message[] = "[hmi] 15-second button failsafe reboot\n";
            DebugLog.capture(message, sizeof(message) - 1);
            esp_restart();
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
String overlayText;uint32_t overlayUntil=0;
lv_disp_t* display=nullptr;lv_indev_t* pointer=nullptr;lv_indev_t* encoder=nullptr;lv_group_t* group=nullptr;
lv_disp_draw_buf_t drawBuffer;lv_disp_drv_t displayDriver;lv_indev_drv_t touchDriver,encoderDriver;
uint32_t accent=0x12a6a6;bool showProgress=true,wifiStatus=false,mqttStatus=false;int fontSize=14;
lv_color_t* pixels=nullptr;lv_obj_t* screen=nullptr;lv_obj_t* center=nullptr;lv_obj_t* ring=nullptr;lv_obj_t* valueLabel=nullptr;lv_obj_t* statusLabel=nullptr;lv_obj_t* titleLabel=nullptr;lv_obj_t* hintLabel=nullptr;
bool detail=false;
lv_obj_t* slider=nullptr;
std::vector<lv_obj_t*> buttons;uint32_t tick=0,drawn=UINT32_MAX,pressedAt=0,edgeAt=0,releasedAt=0;
int oldMenu=-1,oldItem=-1,pending=0,direction=1,steps=2,sensitivity=1,encoderDelta=0;bool active=false,activity=false,buttonRaw=false,buttonDown=false,armed=false,longSent=false,pendingClick=false;
bool touchDown=false,touchLong=false;int16_t startX=0,startY=0,lastX=0,lastY=0;uint32_t touchAt=0;
int itemId=0,rotationDelta=0,swipe=0;double value=0;uint32_t events[12]{};struct EventData{int id=0,delta=0,x=0,y=0,direction=0;double value=0;};EventData data[12];
const char* names[]={"rotated","clockwise","counterclockwise","pressed","longPressed","doublePressed","touchPressed","touchReleased","swipe","selected","valueChanged","touchLong"};
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
void IRAM_ATTR rotate(){portENTER_CRITICAL_ISR(&mux);int delta=-decoder.sample(digitalRead(6),digitalRead(5),steps);const int next=pending+delta;pending=next<-128?-128:next>128?128:next;portEXIT_CRITICAL_ISR(&mux);}
void event(unsigned n,bool capture=true){++events[n];activity=true;if(n<=8||n==11)dashboard.inputActivity();auto* item=model.current();if(dashboard.enabled()){itemId=0;value=0;}else if(capture&&item){itemId=item->id;value=item->value;}data[n]={itemId,rotationDelta,lastX,lastY,swipe,value};}
void activate(){if(dashboard.enabled()){dashboard.press();activity=true;return;}auto* item=model.current();if(item&&item->kind!="menu"&&!detail){detail=true;oldMenu=-1;}if(model.activate())event(9);activity=true;}
void back(){if(dashboard.enabled()){dashboard.back();activity=true;return;}if(detail){detail=false;model.editing=model.confirming=false;oldMenu=-1;++model.revision;}else model.back();activity=true;}
void flush(lv_disp_drv_t*,const lv_area_t* a,lv_color_t* colors){static bool reported=false;if(!reported){reported=true;DebugLog.printf("[hmi] first LVGL flush %dx%d pixel=0x%04x\n",a->x2-a->x1+1,a->y2-a->y1+1,colors[0].full);}panel->drawColor(a->x1,a->y1,a->x2-a->x1+1,a->y2-a->y1+1,reinterpret_cast<uint8_t*>(colors));lv_disp_flush_ready(&displayDriver);}
void touch(lv_indev_drv_t*,lv_indev_data_t* data){int16_t x=0,y=0;bool down=panel->readRawTouch(x,y);data->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    if(down){data->point.x=x;data->point.y=y;lastX=x;lastY=y;
        if(!touchDown){touchDown=true;touchLong=false;touchAt=millis();startX=x;startY=y;event(6);}
        else if(!touchLong&&millis()-touchAt>=800&&abs(x-startX)<20&&abs(y-startY)<20){touchLong=true;event(11);back();}
    }else if(touchDown){touchDown=false;event(7);int dx=lastX-startX,dy=lastY-startY;if(!touchLong&&(abs(dx)>60||abs(dy)>60)){swipe=abs(dx)>abs(dy)?(dx>0?1:-1):(dy>0?2:-2);event(8);if(model.navigate(swipe>0?1:-1))event(10);}}
}
void readEncoder(lv_indev_drv_t*,lv_indev_data_t* data){data->enc_diff=encoderDelta;encoderDelta=0;data->state=LV_INDEV_STATE_RELEASED;}
void clicked(lv_event_t* e){auto* object=lv_event_get_target(e);if(touchLong||abs(lastX-startX)>60||abs(lastY-startY)>60)return;
    if(object==center){if(!detail){model.menu=0;model.selected=0;model.history.clear();oldMenu=-1;++model.revision;}else activate();return;}for(size_t i=0;i<buttons.size();++i)if(object==buttons[i]){auto list=model.visible();if(i<list.size()){model.select(model.items[list[i]].id);activate();}break;}}
uint32_t iconColor(const RotaryHmi::Item& item){String name=item.title.c_str();name.toLowerCase();if(name.indexOf("temp")>=0||item.kind=="gauge")return 0xff5364;if(name.indexOf("wi")>=0||name.indexOf("network")>=0)return 0x52e3b0;if(name.indexOf("setting")>=0||item.kind=="menu")return 0xbdc9e0;if(name.indexOf("led")>=0||name.indexOf("bright")>=0)return 0xffd45a;return 0xbb78ff;}
void adjustValue(double next){auto* item=model.current();if(!item)return;double previous=item->value;if(model.setValue(item->id,next)&&item->value!=previous){itemId=item->id;value=item->value;event(10,false);String title=item->title.c_str();title.toLowerCase();if(title.indexOf("brightness")>=0)panel->brightness(constrain(int(item->value),0,100));}}
lv_obj_t* shape(lv_obj_t* parent,int x,int y,int w,int h,uint32_t color,int radius){auto* o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,radius,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);return o;}
void icon(lv_obj_t* parent,const RotaryHmi::Item& item){
    auto* box=lv_obj_create(parent);lv_obj_remove_style_all(box);lv_obj_set_size(box,44,44);lv_obj_center(box);lv_obj_clear_flag(box,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    String name=item.title.c_str();name.toLowerCase();const uint32_t ink=iconColor(item);
    if(!item.icon.empty()){auto* label=lv_label_create(box);lv_obj_set_width(label,44);lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);lv_label_set_text(label,item.icon.c_str());lv_obj_set_style_text_font(label,&lv_font_montserrat_28,0);lv_obj_set_style_text_color(label,lv_color_hex(ink),0);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label);return;}
    if(name.indexOf("temp")>=0||item.kind=="gauge"){
        shape(box,17,4,10,29,ink,5);shape(box,13,26,18,18,ink,9);shape(box,20,8,4,25,0x12a6a6,2);
        for(int n=0;n<3;++n)shape(box,31,9+n*8,7,3,ink,1);
    }else if(name.indexOf("wi")>=0||name.indexOf("network")>=0){
        for(int n=0;n<3;++n){auto* a=lv_arc_create(box);lv_obj_remove_style_all(a);int size=42-n*12;lv_obj_set_size(a,size,size);lv_obj_set_pos(a,(44-size)/2,3+n*12);lv_arc_set_bg_angles(a,220,320);lv_obj_set_style_arc_color(a,lv_color_hex(ink),LV_PART_MAIN);lv_obj_set_style_arc_width(a,3,LV_PART_MAIN);lv_obj_set_style_arc_opa(a,LV_OPA_COVER,LV_PART_MAIN);lv_obj_clear_flag(a,LV_OBJ_FLAG_CLICKABLE);}
        shape(box,19,35,6,6,ink,3);
    }else if(name.indexOf("setting")>=0||item.kind=="menu"){
        auto* wheel=shape(box,9,9,26,26,ink,13);shape(wheel,7,7,12,12,0x162638,6);
        for(int p:{2,36}){shape(box,19,p,6,6,ink,2);shape(box,p,19,6,6,ink,2);}
        for(int x:{6,32})for(int y:{6,32})shape(box,x,y,6,6,ink,2);
    }else{
        for(int n=0;n<3;++n){int x=7+n*13;shape(box,x,5,3,34,ink,2);shape(box,x-4,10+(n%2)*14,11,9,ink,4);}
    }
}
lv_obj_t* textLabel(lv_obj_t* parent,int y,int width,const lv_font_t* font,uint32_t color){auto* label=lv_label_create(parent);lv_obj_set_width(label,width);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(label,font,0);lv_obj_set_style_text_color(label,lv_color_hex(color),0);lv_label_set_long_mode(label,LV_LABEL_LONG_WRAP);lv_obj_align(label,LV_ALIGN_TOP_MID,0,y);return label;}
void rebuild(){lv_obj_clean(screen);buttons.clear();slider=nullptr;auto list=model.visible();const bool listView=!detail&&model.menu!=0;
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x000000),0);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);lv_obj_set_style_text_color(screen,lv_color_hex(0xe4f2ff),0);lv_obj_set_style_pad_all(screen,0,0);lv_obj_set_style_border_width(screen,0,0);lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    ring=lv_arc_create(screen);lv_obj_remove_style_all(ring);lv_obj_set_size(ring,detail?360:456,detail?360:456);lv_obj_center(ring);lv_arc_set_bg_angles(ring,0,360);lv_arc_set_range(ring,0,100);lv_obj_set_style_arc_color(ring,lv_color_hex(0x1c2d42),LV_PART_MAIN);lv_obj_set_style_arc_opa(ring,LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_arc_width(ring,detail?8:2,LV_PART_MAIN);lv_obj_set_style_arc_color(ring,lv_color_hex(accent),LV_PART_INDICATOR);lv_obj_set_style_arc_opa(ring,LV_OPA_COVER,LV_PART_INDICATOR);lv_obj_set_style_arc_width(ring,detail?8:3,LV_PART_INDICATOR);lv_obj_clear_flag(ring,LV_OBJ_FLAG_CLICKABLE);lv_obj_remove_style(ring,nullptr,LV_PART_KNOB);
    if(!detail){lv_obj_set_style_arc_color(ring,lv_color_hex(0x41434c),LV_PART_MAIN);lv_obj_set_style_arc_width(ring,5,LV_PART_MAIN);}else lv_arc_set_bg_angles(ring,135,45);
    center=lv_btn_create(screen);lv_obj_remove_style_all(center);lv_obj_set_size(center,detail?276:124,detail?276:124);lv_obj_center(center);lv_obj_set_style_radius(center,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_color(center,lv_color_hex(detail?0x000000:0x092c4c),0);lv_obj_set_style_bg_opa(center,LV_OPA_COVER,0);lv_obj_set_style_border_color(center,lv_color_hex(0x39baff),0);lv_obj_set_style_border_width(center,detail?0:3,0);lv_obj_clear_flag(center,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(center,clicked,LV_EVENT_CLICKED,nullptr);
    if(!detail){lv_obj_set_style_shadow_color(center,lv_color_hex(0x008dff),0);lv_obj_set_style_shadow_width(center,22,0);lv_obj_set_style_shadow_opa(center,LV_OPA_60,0);}
    titleLabel=textLabel(screen,detail||listView?65:312,300,&lv_font_montserrat_18,0xe4f2ff);
    valueLabel=textLabel(center,detail?101:33,detail?244:110,&lv_font_montserrat_48,0xe4f2ff);
    hintLabel=textLabel(detail?center:screen,detail?169:344,detail?236:210,&lv_font_montserrat_14,0x8fa9bf);
    if(!detail&&!listView){size_t count=std::min<size_t>(list.size(),8);for(size_t i=0;i<count;++i){auto* btn=lv_btn_create(screen);lv_obj_remove_style_all(btn);buttons.push_back(btn);double angle=(-90.+360.*i/count)*3.141592653589793/180.;
        lv_obj_set_size(btn,78,78);lv_obj_set_pos(btn,201+int(std::cos(angle)*157),201+int(std::sin(angle)*157));lv_obj_set_style_radius(btn,LV_RADIUS_CIRCLE,0);lv_obj_set_style_bg_opa(btn,LV_OPA_COVER,0);lv_obj_set_style_border_width(btn,2,0);lv_obj_clear_flag(btn,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_event_cb(btn,clicked,LV_EVENT_CLICKED,nullptr);icon(btn,model.items[list[i]]);}}
    if(listView){lv_obj_add_flag(center,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(ring,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(hintLabel,LV_OBJ_FLAG_HIDDEN);auto* rows=lv_obj_create(screen);lv_obj_remove_style_all(rows);lv_obj_set_pos(rows,70,104);lv_obj_set_size(rows,340,310);lv_obj_set_scroll_dir(rows,LV_DIR_VER);for(size_t i=0;i<list.size();++i){auto* btn=lv_btn_create(rows);lv_obj_remove_style_all(btn);lv_obj_set_pos(btn,0,int(i)*62);lv_obj_set_size(btn,340,54);lv_obj_set_style_radius(btn,27,0);lv_obj_set_style_bg_opa(btn,LV_OPA_COVER,0);lv_obj_set_style_border_width(btn,1,0);buttons.push_back(btn);lv_obj_add_event_cb(btn,clicked,LV_EVENT_CLICKED,nullptr);auto* label=lv_label_create(btn);lv_label_set_text(label,model.items[list[i]].title.c_str());lv_obj_set_style_text_font(label,&lv_font_montserrat_18,0);lv_obj_align(label,LV_ALIGN_LEFT_MID,22,0);auto* arrow=lv_label_create(btn);lv_label_set_text(arrow,LV_SYMBOL_RIGHT);lv_obj_align(arrow,LV_ALIGN_RIGHT_MID,-20,0);}}
    if(detail&&model.current()&&model.current()->kind=="value"){
        slider=lv_slider_create(screen);lv_obj_set_size(slider,180,14);lv_obj_set_pos(slider,150,351);lv_slider_set_range(slider,0,100);lv_obj_set_style_bg_color(slider,lv_color_hex(0x39baff),LV_PART_INDICATOR);lv_obj_set_style_bg_color(slider,lv_color_hex(0xe4f2ff),LV_PART_KNOB);lv_obj_add_event_cb(slider,[](lv_event_t* e){auto* item=model.current();if(item){double raw=item->minimum+(item->maximum-item->minimum)*lv_slider_get_value(lv_event_get_target(e))/100.;adjustValue(item->minimum+std::round((raw-item->minimum)/item->step)*item->step);}},LV_EVENT_VALUE_CHANGED,nullptr);
        for(int sign:{-1,1}){auto* button=lv_btn_create(screen);lv_obj_set_size(button,54,54);lv_obj_set_pos(button,sign<0?82:344,330);lv_obj_set_style_radius(button,27,0);lv_obj_set_style_bg_color(button,lv_color_hex(0x202733),0);auto* label=lv_label_create(button);lv_label_set_text(label,sign<0?LV_SYMBOL_MINUS:LV_SYMBOL_PLUS);lv_obj_center(label);lv_obj_add_event_cb(button,[](lv_event_t* e){auto* item=model.current();if(item)adjustValue(item->value+intptr_t(lv_event_get_user_data(e))*item->step);},LV_EVENT_CLICKED,reinterpret_cast<void*>(intptr_t(sign)));}
    }
    if(detail||model.menu!=0){auto* button=lv_btn_create(screen);lv_obj_remove_style_all(button);lv_obj_set_size(button,96,40);lv_obj_set_pos(button,192,420);lv_obj_set_style_radius(button,20,0);lv_obj_set_style_bg_color(button,lv_color_hex(0x18283a),0);lv_obj_set_style_bg_opa(button,LV_OPA_COVER,0);auto* label=lv_label_create(button);lv_label_set_text(label,LV_SYMBOL_LEFT " Back");lv_obj_center(label);lv_obj_add_event_cb(button,[](lv_event_t*){back();},LV_EVENT_CLICKED,nullptr);}
    statusLabel=textLabel(screen,27,160,&lv_font_montserrat_14,0xaebdcc);oldMenu=model.menu;oldItem=model.current()?model.current()->id:-1;lv_group_remove_all_objs(group);if(!listView)lv_group_add_obj(group,center);
}
void render(){if(dashboard.enabled()){drawn=model.revision;return;}if(oldMenu!=model.menu||(detail&&model.current()&&oldItem!=model.current()->id))rebuild();auto list=model.visible();auto* item=model.current();if(!item){lv_label_set_text(titleLabel,"Empty menu");return;}
    const bool listView=!detail&&model.menu!=0;const char* title=item->title.c_str();if(listView)for(auto& parent:model.items)if(parent.kind=="menu"&&parent.submenu==model.menu){title=parent.title.c_str();break;}
    lv_label_set_text(titleLabel,title);char number[64];number[0]=0;
    if(!detail)snprintf(number,sizeof(number),"%s",LV_SYMBOL_HOME);
    else if(item->kind=="value"||item->kind=="gauge")snprintf(number,sizeof(number),"%.1f %s",item->value,item->unit.c_str());
    else if(item->kind=="menu")snprintf(number,sizeof(number),"%s",LV_SYMBOL_LIST);
    else if(item->title.find("Wi")!=std::string::npos)snprintf(number,sizeof(number),"%s",wifiStatus?"Online":"Offline");
    else snprintf(number,sizeof(number),"%s",detail?LV_SYMBOL_OK:LV_SYMBOL_RIGHT);
    lv_label_set_text(valueLabel,overlayText.length()?overlayText.c_str():number);
    String hint=model.confirming?"Press to confirm":model.editing?"Turn to adjust\nPress to save":detail?"Hold to go back":"Press knob to open";
    if(detail&&item->title.find("Wi")!=std::string::npos){hint=wifiStatus?WiFi.SSID()+"\n"+WiFi.localIP().toString():WiFi.softAPSSID()+"\n"+WiFi.softAPIP().toString();lv_obj_set_style_text_font(valueLabel,&lv_font_montserrat_28,0);}else lv_obj_set_style_text_font(valueLabel,&lv_font_montserrat_48,0);
    lv_label_set_text(hintLabel,hint.c_str());
    double range=item->maximum-item->minimum;lv_arc_set_value(ring,detail&&range>0?int(100*(item->value-item->minimum)/range):0);if(detail&&!showProgress)lv_obj_add_flag(ring,LV_OBJ_FLAG_HIDDEN);
    if(slider&&range>0)lv_slider_set_value(slider,int(100*(item->value-item->minimum)/range),LV_ANIM_OFF);
    for(size_t i=0;i<buttons.size();++i){bool selected=int(i)==model.selected;uint32_t color=listView?0x39baff:iconColor(model.items[list[i]]);lv_obj_set_style_bg_color(buttons[i],lv_color_hex(selected?0x0e2234:0x090d14),0);lv_obj_set_style_border_color(buttons[i],lv_color_hex(selected?color:listView?0x202733:color),0);lv_obj_set_style_border_width(buttons[i],selected?3:1,0);if(!listView){lv_obj_set_style_shadow_color(buttons[i],lv_color_hex(color),0);lv_obj_set_style_shadow_width(buttons[i],selected?20:12,0);lv_obj_set_style_shadow_opa(buttons[i],selected?LV_OPA_60:LV_OPA_30,0);}else if(selected)lv_obj_scroll_to_view(buttons[i],LV_ANIM_OFF);}
    time_t now=time(nullptr);struct tm clock{};char header[32];if(now>1577836800&&localtime_r(&now,&clock))strftime(header,sizeof(header),"%H:%M",&clock);else snprintf(header,sizeof(header),"--:--");String status=String(header)+"  "+(wifiStatus?LV_SYMBOL_WIFI:"AP");lv_label_set_text(statusLabel,status.c_str());drawn=model.revision;
}
}
bool configure(const String& raw,String& error,bool validateOnly){if(raw.length()>16384){error="Circular menu exceeds 16 KiB";return false;}PanelJsonAllocator allocator;JsonDocument doc(&allocator);
    const char* demo=R"json({"schemaVersion":1,"items":[{"id":1,"title":"Temperature","kind":"gauge","value":0,"unit":"C"},{"id":2,"title":"Wi-Fi","kind":"text"},{"id":3,"title":"LED Control","kind":"value","value":50},{"id":4,"title":"Settings","kind":"menu","submenu":1},{"id":5,"parent":1,"title":"Brightness","kind":"value","value":80}]})json";
    if(deserializeJson(doc,raw.length()?raw.c_str():demo)||(!doc["schemaVersion"].is<int>()||doc["schemaVersion"]!=1)||!doc["items"].is<JsonArrayConst>()||doc["items"].size()>32){error="Invalid circular menu (schema 1, up to 32 items)";return false;}
    String color=doc["accent"]|"#12a6a6";int configuredFont=doc["fontSize"]|14;
    if(color.length()!=7||color[0]!='#'||(configuredFont!=14&&configuredFont!=18)){error="Accent must be #RRGGBB; font size 14 or 18";return false;}
    for(unsigned n=1;n<7;++n)if(!isxdigit(color[n])){error="Invalid accent color";return false;}
    RotaryHmi::Model next;std::set<int> ids;std::set<int> parents;
    for(JsonObjectConst obj:doc["items"].as<JsonArrayConst>()){RotaryHmi::Item i;i.id=obj["id"]|0;i.parent=obj["parent"]|0;i.title=(obj["title"]|"");i.icon=(obj["icon"]|"");i.kind=(obj["kind"]|"action");i.submenu=obj["submenu"]|-1;i.minimum=obj["minimum"]|0.;i.maximum=obj["maximum"]|100.;i.step=obj["step"]|1.;i.value=obj["value"]|0.;i.unit=obj["unit"]|"";i.variable=obj["variable"]|"";
        bool types=obj["id"].is<int>()&&obj["title"].is<const char*>();
        for(const char* key:{"parent","submenu"})if(!obj[key].isNull())types&=obj[key].is<int>();
        for(const char* key:{"icon","kind","unit","variable"})if(!obj[key].isNull())types&=obj[key].is<const char*>();
        for(const char* key:{"minimum","maximum","step","value"})if(!obj[key].isNull())types&=obj[key].is<double>();
        if(!types){error="Invalid circular menu field type";return false;}
        if(i.id<1||i.id>65535||i.parent>65535||i.submenu>65535||!ids.insert(i.id).second||i.parent<0||i.title.empty()||i.title.size()>48||i.icon.size()>32||i.unit.size()>16||i.variable.size()>96||!std::isfinite(i.value)||!std::isfinite(i.minimum)||!std::isfinite(i.maximum)||!std::isfinite(i.step)||i.maximum<=i.minimum||i.step<=0||!(i.kind=="action"||i.kind=="menu"||i.kind=="value"||i.kind=="gauge"||i.kind=="text"||i.kind=="confirm")){error="Invalid circular menu item";return false;}i.value=std::max(i.minimum,std::min(i.maximum,i.value));next.items.push_back(i);parents.insert(i.parent);}
    if(!parents.count(0)){error="Circular menu requires a root item";return false;}
    for(int parent:parents){int count=0;for(auto& i:next.items)if(i.parent==parent)++count;if(count>8){error="At most eight items per circular menu";return false;}}
    for(auto& i:next.items)if(i.kind=="menu"&&(!parents.count(i.submenu)||i.submenu==i.parent)){error="Submenu must name an existing, different menu";return false;}
    // Every submenu must be reachable from root and acyclic, with bounded depth.
    std::set<int> visited,visiting;std::function<bool(int,int)> walk=[&](int parent,int depth){if(depth>8||visiting.count(parent))return false;if(visited.count(parent))return true;visiting.insert(parent);for(auto& i:next.items)if(i.parent==parent&&i.kind=="menu")if(!walk(i.submenu,depth+1))return false;visiting.erase(parent);visited.insert(parent);return true;};
    if(!walk(0,0)||visited!=parents){error="Submenus must form an acyclic tree reachable from root (depth <= 8)";return false;}
    for(const char* key:{"stepsPerDetent","sensitivity","fontSize"})if(!doc[key].isNull()&&!doc[key].is<int>()){error="Rotary settings must be integers";return false;}
    for(const char* key:{"reverse","showProgress","systemDashboard"})if(!doc[key].isNull()&&!doc[key].is<bool>()){error="Rotary options must be Boolean";return false;}
    if(!doc["systemTimeoutSeconds"].isNull()&&(!doc["systemTimeoutSeconds"].is<int>()||doc["systemTimeoutSeconds"].as<int>()<15||doc["systemTimeoutSeconds"].as<int>()>3600)){error="System home timeout must be 15–3600 seconds";return false;}
    int configuredSteps=doc["stepsPerDetent"]|2,configuredSensitivity=doc["sensitivity"]|1;
    if((configuredSteps!=1&&configuredSteps!=2&&configuredSteps!=4)||configuredSensitivity<1||configuredSensitivity>8){error="Invalid rotary sensitivity or steps per detent";return false;}
    if(validateOnly)return true;
    Guard guard;if(!guard){error="HMI lock unavailable";return false;}
    portENTER_CRITICAL(&mux);steps=configuredSteps;direction=doc["reverse"]==true?-1:1;sensitivity=configuredSensitivity;pending=0;decoder.partial=0;portEXIT_CRITICAL(&mux);
    accent=strtoul(color.c_str()+1,nullptr,16);fontSize=configuredFont;showProgress=doc["showProgress"]|true;
    model=std::move(next);detail=false;oldMenu=-1;drawn=UINT32_MAX;return true;
}
bool available(){Guard guard;return guard&&active;}
bool begin(PanelDisplay& p,uint8_t rotation,const String& configuration){Guard guard;if(!guard)return false;
    pinMode(0,INPUT_PULLUP);
    if(!failsafeTask&&xTaskCreate(buttonFailsafe,"knob-failsafe",2048,nullptr,1,&failsafeTask)!=pdPASS)DebugLog.println("[hmi] failsafe task allocation failed");
    end();panel=&p;String error;if(!configure(configuration,error)){DebugLog.println(error);return false;}
    static bool initialized=false;if(!initialized){lv_init();initialized=true;}pixels=static_cast<lv_color_t*>(heap_caps_malloc(480*10*sizeof(lv_color_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));if(!pixels)return false;
    lv_disp_draw_buf_init(&drawBuffer,pixels,nullptr,480*10);lv_disp_drv_init(&displayDriver);displayDriver.hor_res=480;displayDriver.ver_res=480;displayDriver.draw_buf=&drawBuffer;displayDriver.flush_cb=flush;displayDriver.sw_rotate=1;display=lv_disp_drv_register(&displayDriver);if(!display){end();return false;}lv_disp_set_rotation(display,static_cast<lv_disp_rot_t>(rotation));
    // No default LVGL theme is compiled in. Initialize the shared theme before
    // creating widgets, otherwise backgrounds remain transparent over white.
    lv_disp_set_theme(display,elmaPanelTheme(display));
    screen=lv_disp_get_scr_act(display);lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);group=lv_group_create();lv_indev_drv_init(&touchDriver);touchDriver.disp=display;touchDriver.type=LV_INDEV_TYPE_POINTER;touchDriver.read_cb=touch;pointer=lv_indev_drv_register(&touchDriver);
    lv_indev_drv_init(&encoderDriver);encoderDriver.disp=display;encoderDriver.type=LV_INDEV_TYPE_ENCODER;encoderDriver.read_cb=readEncoder;encoder=lv_indev_drv_register(&encoderDriver);if(!pointer||!encoder||!group){end();return false;}lv_indev_set_group(encoder,group);
    pinMode(6,INPUT_PULLUP);pinMode(5,INPUT_PULLUP);
    decoder.reset(digitalRead(6),digitalRead(5));attachInterrupt(6,rotate,CHANGE);attachInterrupt(5,rotate,CHANGE);armed=digitalRead(0)==HIGH;buttonRaw=buttonDown=false;pendingClick=false;longSent=false;touchDown=false;tick=millis();active=true;dashboard.begin(screen,p,configuration);render();DebugLog.printf("[hmi] ready items=%u rotation=%u\n",unsigned(model.items.size()),rotation);return true;
}
void end(){Guard guard;if(!guard)return;dashboard.end();detachInterrupt(6);detachInterrupt(5);active=false;if(pointer)lv_indev_delete(pointer);if(encoder)lv_indev_delete(encoder);if(group)lv_group_del(group);if(display)lv_disp_remove(display);if(pixels)heap_caps_free(pixels);pointer=encoder=nullptr;group=nullptr;display=nullptr;pixels=nullptr;panel=nullptr;buttons.clear();screen=center=ring=valueLabel=nullptr;}
void loop(){Guard guard;if(!guard||!active)return;uint32_t now=millis();if(overlayText.length()&&int32_t(now-overlayUntil)>=0){overlayText="";++model.revision;}lv_tick_inc(now-tick);tick=now;
    portENTER_CRITICAL(&mux);int delta=pending;pending=0;portEXIT_CRITICAL(&mux);if(delta){delta*=direction;rotationDelta=delta;event(0);event(delta>0?1:2);encoderDelta=delta;
        if(dashboard.enabled())dashboard.rotate(delta);else if(model.navigate(delta*sensitivity))event(10);}
    bool down=digitalRead(0)==LOW;if(!armed){if(!down)armed=true;}else{
        if(down!=buttonRaw){buttonRaw=down;edgeAt=now;}if(now-edgeAt>=25&&down!=buttonDown){buttonDown=down;
            if(down){pressedAt=now;longSent=false;}else if(!longSent){if(pendingClick&&now-releasedAt<300){pendingClick=false;event(5);}else{pendingClick=true;releasedAt=now;}}}
        if(buttonDown&&!longSent&&now-pressedAt>=800){longSent=true;pendingClick=false;event(4);back();}
        if(pendingClick&&!buttonDown&&now-releasedAt>=300){pendingClick=false;event(3);activate();}}
    if(drawn!=model.revision)render();lv_timer_handler();
}
bool takeActivity(){Guard guard;if(!guard)return false;bool result=activity;activity=false;return result;}
void systemState(const AppStateSnapshot& state,const std::function<void(const String&,JsonObject)>& snapshot,const std::function<bool(const String&,JsonVariantConst,String&)>& command){Guard guard;if(guard&&active){bool wasVisible=dashboard.enabled();dashboard.loop(state,snapshot,command);if(wasVisible&&!dashboard.enabled()){oldMenu=-1;++model.revision;}}}
void networkStatus(bool wifi,bool mqtt){Guard guard;if(!guard)return;if(wifiStatus!=wifi||mqttStatus!=mqtt){wifiStatus=wifi;mqttStatus=mqtt;++model.revision;}}
void temporaryText(const String& text,uint32_t durationMs){Guard guard;if(!guard)return;overlayText=text;overlayUntil=millis()+std::min<uint32_t>(durationMs,0x7fffffff);++model.revision;}
void snapshot(JsonObject obj){Guard guard;if(!guard){obj["available"]=false;return;}obj["available"]=active;for(unsigned n=0;n<12;++n){obj["events"][names[n]]=events[n];auto event=obj["data"][names[n]].to<JsonObject>();event["itemId"]=data[n].id;event["delta"]=data[n].delta;event["value"]=data[n].value;event["x"]=data[n].x;event["y"]=data[n].y;event["direction"]=data[n].direction;}obj["itemId"]=itemId;obj["delta"]=rotationDelta;obj["value"]=value;obj["x"]=lastX;obj["y"]=lastY;obj["direction"]=swipe;obj["menu"]=model.menu;obj["editing"]=model.editing;obj["pressed"]=buttonDown;}
bool command(JsonVariantConst args,String& error){Guard guard;if(!guard){error="HMI lock unavailable";return false;}if(!active){error="Round HMI is not initialized";return false;}String action=args["action"]|"";
    if(action=="navigate"){String how=args["navigation"]|"next";if(how=="back")back();else if(how=="activate")activate();else if(dashboard.enabled())dashboard.rotate(how=="previous"?-1:1);else if(model.navigate(how=="previous"?-1:1))event(10);}
    else if(action=="select"){if(!model.select(args["itemId"]|0)){error="Item is outside the current menu";return false;}}
    else if(action=="value"||action=="text"||action=="icon"||action=="gauge"||action=="progress"||action=="confirm"){
        int id=args["itemId"]|0;auto found=std::find_if(model.items.begin(),model.items.end(),[id](const RotaryHmi::Item& i){return i.id==id;});if(found==model.items.end()){error="Unknown circular menu item";return false;}
        if(action=="value"||action=="gauge"||action=="progress"){const double previous=found->value;if(!model.setValue(id,args["value"]|0.)){error="Invalid display value";return false;}value=found->value;itemId=id;if(value!=previous)event(10,false);}
        if(action=="text"){String text=args["text"]|"";if(text.length()>48){error="Text exceeds 48 bytes";return false;}found->title=text.c_str();++model.revision;oldMenu=-1;}
        if(action=="icon"){String icon=args["icon"]|"";if(icon.length()>32){error="Icon exceeds 32 bytes";return false;}found->icon=icon.c_str();++model.revision;}
        if(action=="confirm"){if(found->kind!="confirm"||!model.select(id)){error="Confirmation requires a visible confirm item";return false;}model.confirming=true;++model.revision;}
    }else if(action=="brightness")panel->brightness(constrain(args["value"]|80,0,100));
    else if(action=="create"){String config;if(args["configuration"].is<const char*>())config=args["configuration"].as<String>();else serializeJson(args["configuration"],config);if(!configure(config,error,true))return false;dashboard.useCustomMenu();return configure(config,error);}
    else if(action=="add"||action=="submenu"){PanelJsonAllocator allocator;JsonDocument doc(&allocator);doc["schemaVersion"]=1;doc["stepsPerDetent"]=steps;doc["sensitivity"]=sensitivity;doc["reverse"]=direction<0;char color[8];snprintf(color,sizeof(color),"#%06lx",(unsigned long)accent);doc["accent"]=color;doc["fontSize"]=fontSize;doc["showProgress"]=showProgress;
        auto items=doc["items"].to<JsonArray>();for(const auto& i:model.items){auto o=items.add<JsonObject>();o["id"]=i.id;o["parent"]=i.parent;o["title"]=i.title;o["icon"]=i.icon;o["kind"]=i.kind;o["submenu"]=i.submenu;o["minimum"]=i.minimum;o["maximum"]=i.maximum;o["step"]=i.step;o["value"]=i.value;o["unit"]=i.unit;o["variable"]=i.variable;}
        auto added=items.add<JsonObject>();for(const char* key:{"id","parent","title","kind","submenu","minimum","maximum","step","value","unit","icon"})if(!args[key].isNull())added[key].set(args[key]);added["id"]=args["itemId"]|0;
        if(action=="submenu"){added["kind"]="menu";auto child=items.add<JsonObject>();child["id"]=args["childId"]|((args["itemId"]|0)+1000);child["parent"]=args["submenu"]|0;child["title"]="New item";child["kind"]="action";}
        String config;serializeJson(doc,config);return configure(config,error);}
    else{error="Unknown circular HMI command";return false;}activity=true;return true;
}
}
#else
namespace RoundHmi {
bool available(){return false;}bool begin(PanelDisplay&,uint8_t,const String&){return false;}void end(){}void loop(){}void networkStatus(bool,bool){}bool takeActivity(){return false;}
void temporaryText(const String&,uint32_t){}
void systemState(const AppStateSnapshot&,const std::function<void(const String&,JsonObject)>&,const std::function<bool(const String&,JsonVariantConst,String&)>&){}
void snapshot(JsonObject obj){obj["available"]=false;}bool command(JsonVariantConst,String& error){error="Round HMI is unavailable for this firmware";return false;}bool configure(const String&,String&,bool){return false;}
}
#endif
