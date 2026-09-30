#include "panel_dashboard.h"
#if APP_HAS_ONBOARD_PANEL
#include "panel_display.h"

PanelDashboard::~PanelDashboard() {
    if(input_)lv_indev_delete(input_);
    if(display_)lv_disp_remove(display_);
    if(pixels_)heap_caps_free(pixels_);
}
bool PanelDashboard::begin(uint8_t rotation) {
    static bool initialized=false;
    if(!initialized){lv_init();initialized=true;}
    pixels_=static_cast<lv_color_t*>(heap_caps_malloc(240*20*sizeof(lv_color_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
    if(!pixels_)return false;
    lv_disp_draw_buf_init(&drawBuffer_,pixels_,nullptr,240*20);
    lv_disp_drv_init(&displayDriver_);
    displayDriver_.hor_res=240;displayDriver_.ver_res=320;displayDriver_.draw_buf=&drawBuffer_;
    displayDriver_.flush_cb=flush;displayDriver_.user_data=this;displayDriver_.sw_rotate=1;
    display_=lv_disp_drv_register(&displayDriver_);if(!display_)return false;
    lv_disp_set_rotation(display_,static_cast<lv_disp_rot_t>(rotation));
    lv_indev_drv_init(&inputDriver_);inputDriver_.type=LV_INDEV_TYPE_POINTER;
    inputDriver_.read_cb=touch;inputDriver_.user_data=this;inputDriver_.disp=display_;
    input_=lv_indev_drv_register(&inputDriver_);
    screen_=lv_disp_get_scr_act(display_);
    lv_obj_set_style_bg_color(screen_,lv_color_hex(0x111827),0);
    lv_obj_set_style_text_color(screen_,lv_color_hex(0xf3f4f6),0);
    lv_obj_set_style_pad_all(screen_,6,0);
    lv_obj_set_flex_flow(screen_,LV_FLEX_FLOW_COLUMN);
    header_=lv_label_create(screen_);lv_label_set_text(header_,"ELMA-IoT");lv_obj_set_width(header_,LV_PCT(100));
    lv_obj_t* menu=lv_dropdown_create(screen_);lv_obj_set_width(menu,LV_PCT(100));lv_obj_set_height(menu,44);
    lv_dropdown_set_options(menu,"Home\nAudio\nLogics\nNetwork\nDisplay\nDevice");
    lv_obj_add_event_cb(menu,[](lv_event_t* e){auto* self=static_cast<PanelDashboard*>(lv_event_get_user_data(e));self->page(lv_dropdown_get_selected(lv_event_get_target(e)));},LV_EVENT_VALUE_CHANGED,this);
    body_=lv_obj_create(screen_);lv_obj_set_width(body_,LV_PCT(100));lv_obj_set_flex_grow(body_,1);
    lv_obj_set_flex_flow(body_,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(body_,8,0);
    lv_obj_set_style_bg_color(body_,lv_color_hex(0x1f2937),0);lv_obj_set_style_text_color(body_,lv_color_hex(0xf3f4f6),0);
    notice_=lv_label_create(screen_);lv_obj_set_width(notice_,LV_PCT(100));lv_label_set_long_mode(notice_,LV_LABEL_LONG_WRAP);lv_label_set_text(notice_,"");
    keyboard_=lv_keyboard_create(lv_disp_get_layer_top(display_));lv_obj_set_height(keyboard_,LV_PCT(48));lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(keyboard_,[](lv_event_t* e){if(lv_event_get_code(e)==LV_EVENT_READY||lv_event_get_code(e)==LV_EVENT_CANCEL){auto* self=static_cast<PanelDashboard*>(lv_event_get_user_data(e));lv_obj_add_flag(self->keyboard_,LV_OBJ_FLAG_HIDDEN);lv_obj_set_height(self->screen_,lv_disp_get_ver_res(self->display_));}},LV_EVENT_ALL,this);
    tick_=millis();page(0);return true;
}
void PanelDashboard::flush(lv_disp_drv_t* driver,const lv_area_t* area,lv_color_t* colors) {
    auto* self=static_cast<PanelDashboard*>(driver->user_data);
    self->panel_.drawColor(area->x1,area->y1,area->x2-area->x1+1,area->y2-area->y1+1,reinterpret_cast<uint8_t*>(colors));
    lv_disp_flush_ready(driver);
}
void PanelDashboard::touch(lv_indev_drv_t* driver,lv_indev_data_t* data) {
    auto* self=static_cast<PanelDashboard*>(driver->user_data);int16_t x,y;
    if(self->panel_.readRawTouch(x,y)){data->point.x=x;data->point.y=y;data->state=LV_INDEV_STATE_PRESSED;self->touched_=true;}
    else data->state=LV_INDEV_STATE_RELEASED;
}
lv_obj_t* PanelDashboard::label(const String& text) {
    auto* object=lv_label_create(body_);lv_obj_set_width(object,LV_PCT(100));lv_label_set_long_mode(object,LV_LABEL_LONG_WRAP);lv_label_set_text(object,text.c_str());return object;
}
void PanelDashboard::button(const char* text,const char* key) {
    auto* object=lv_btn_create(body_);lv_obj_set_size(object,LV_PCT(100),44);
    auto* caption=lv_label_create(object);lv_label_set_text(caption,text);lv_obj_center(caption);
    fields_[key]=object;lv_obj_add_event_cb(object,event,LV_EVENT_CLICKED,this);
}
void PanelDashboard::slider(const char* text,const char* key,int value) {
    label(text);auto* object=lv_slider_create(body_);lv_obj_set_size(object,LV_PCT(92),24);
    lv_obj_set_style_pad_row(body_,16,0);
    lv_slider_set_range(object,0,100);lv_slider_set_value(object,value,LV_ANIM_OFF);
    lv_obj_set_ext_click_area(object,10);fields_[key]=object;lv_obj_add_event_cb(object,event,LV_EVENT_RELEASED,this);
}
void PanelDashboard::textField(const char* caption,const char* key,const String& value,bool secret) {
    label(caption);auto* object=lv_textarea_create(body_);lv_obj_set_width(object,LV_PCT(100));lv_textarea_set_one_line(object,true);
    lv_textarea_set_max_length(object,secret?64:192);lv_textarea_set_password_mode(object,secret);lv_textarea_set_text(object,value.c_str());
    fields_[key]=object;
    lv_obj_add_event_cb(object,[](lv_event_t* e){auto* self=static_cast<PanelDashboard*>(lv_event_get_user_data(e));lv_keyboard_set_textarea(self->keyboard_,lv_event_get_target(e));lv_obj_clear_flag(self->keyboard_,LV_OBJ_FLAG_HIDDEN);lv_obj_set_height(self->screen_,lv_disp_get_ver_res(self->display_)*52/100);lv_obj_scroll_to_view_recursive(lv_event_get_target(e),LV_ANIM_ON);},LV_EVENT_CLICKED,this);
}
void PanelDashboard::page(int value) {
    page_=value;lv_keyboard_set_textarea(keyboard_,nullptr);lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(screen_,lv_disp_get_ver_res(display_));fields_.clear();lv_obj_clean(body_);
    if(value==0){fields_["summary"]=label("Connecting...");label("Open the device IP in a browser for the full editor and files.");}
    if(value==1){fields_["playback"]=label("");slider("Volume %","volume",0);textField("Stream / file URL","url",state_["lastUrl"]|"");button("Play","play");button("Stop audio","stop");}
    if(value==2){fields_["logicStatus"]=label("");button("Play Logics","playing");button("Pause Logics","paused");button("Stop Logics","stopped");
        for(JsonObjectConst group:state_["logics"]["groups"].as<JsonArrayConst>()){
            String id=group["id"]|"";label(group["name"]|id.c_str());
            for(const char* mode:{"playing","paused","stopped"}){String key="group:"+id+":"+mode;button(mode,key.c_str());}
        }
    }
    if(value==3){fields_["network"]=label("");textField("Wi-Fi SSID","ssid",state_["ssid"]|"");textField("Wi-Fi password","wifiPassword","",true);button("Save Wi-Fi","wifiSave");
        textField("MQTT host","host",state_["mqttHost"]|"");textField("MQTT port","port",String(state_["mqttPort"]|1883));textField("MQTT username","username",state_["mqttUsername"]|"");textField("MQTT password","mqttPassword","",true);label("Blank passwords keep existing values.");button("Save MQTT","mqttSave");button("Connect MQTT","mqttConnect");button("Disconnect MQTT","mqttDisconnect");button("Rediscover devices","mqttRediscover");}
    if(value==4){slider("Backlight %","brightness",state_["brightness"]|100);label("Rotation");auto* rotation=lv_dropdown_create(body_);lv_obj_set_size(rotation,LV_PCT(100),44);lv_dropdown_set_options(rotation,"0 degrees\n90 degrees\n180 degrees\n270 degrees");lv_dropdown_set_selected(rotation,(state_["rotation"]|0)/90);fields_["rotation"]=rotation;lv_obj_add_event_cb(rotation,event,LV_EVENT_VALUE_CHANGED,this);label("Touch and interface mode can also be changed in the web interface.");}
    if(value==5){fields_["device"]=label("");button("Check for firmware update","otaCheck");button("Restart device","rebootConfirm");}
    update();
}
void PanelDashboard::queue(const String& action,JsonVariantConst args) {
    if(commands_.size()>=8)return;JsonDocument command;command["action"]=action;command["args"].set(args);String encoded;serializeJson(command,encoded);commands_.push_back(encoded);
}
void PanelDashboard::event(lv_event_t* e) {
    auto* self=static_cast<PanelDashboard*>(lv_event_get_user_data(e));String key;
    for(auto& item:self->fields_)if(item.second==lv_event_get_target(e)){key=item.first;break;}
    if(key.isEmpty())return;JsonDocument args;String action=key;
    auto text=[self](const char* field){return String(lv_textarea_get_text(self->fields_.at(field)));};
    if(key=="volume")args["value"]=lv_slider_get_value(lv_event_get_target(e));
    else if(key=="brightness"){action="settings";args["oled"]["brightness"]=lv_slider_get_value(lv_event_get_target(e));}
    else if(key=="rotation"){action="settings";args["oled"]["rotation"]=lv_dropdown_get_selected(lv_event_get_target(e))*90;}
    else if(key=="play")args["url"]=text("url");
    else if(key=="playing"||key=="paused"||key=="stopped"){action="logics";args["mode"]=key;}
    else if(key.startsWith("group:")){action="logics";int split=key.lastIndexOf(':');args["group"]["id"]=key.substring(6,split);args["group"]["mode"]=key.substring(split+1);}
    else if(key=="wifiSave"){action="settings";args["wifi"]["ssid"]=text("ssid");if(text("wifiPassword").length())args["wifi"]["password"]=text("wifiPassword");}
    else if(key=="mqttSave"){action="settings";long port=text("port").toInt();if(port<1||port>65535){lv_label_set_text(self->notice_,"Port must be 1-65535");return;}args["mqtt"]["host"]=text("host");args["mqtt"]["port"]=port;args["mqtt"]["username"]=text("username");if(text("mqttPassword").length())args["mqtt"]["password"]=text("mqttPassword");}
    else if(key=="rebootConfirm"){self->button("Confirm restart","reboot");return;}
    self->queue(action,args.as<JsonVariantConst>());
}
void PanelDashboard::update() {
    auto set=[this](const char* key,const String& text){auto it=fields_.find(key);if(it!=fields_.end())lv_label_set_text(it->second,text.c_str());};
    set("summary",String(state_["name"]|"ELMA-IoT")+"\n"+(state_["wifiConnected"]==true?"Wi-Fi connected":"Wi-Fi offline / AP")+"\n"+(state_["ip"]|"")+"\nMQTT: "+(state_["mqttConnected"]==true?"connected":"offline")+"\nBattery: "+String(state_["voltage"]|0.0f,2)+" V");
    set("playback",String(state_["title"]|"Idle")+"\n"+(state_["playbackState"]|"idle")+" - "+String(state_["volume"]|0)+"%");
    String logic=String("Logics: ")+(state_["logics"]["mode"]|"stopped");
    for(JsonObjectConst group:state_["logics"]["groups"].as<JsonArrayConst>())logic+="\n"+String(group["name"]|group["id"].as<const char*>())+": "+(group["mode"]|"playing");
    set("logicStatus",logic);
    set("network",String(state_["wifiConnected"]==true?"Wi-Fi connected":"Wi-Fi offline")+"\n"+(state_["ip"]|"")+"\nMQTT: "+(state_["mqttConnected"]==true?"connected":"offline"));
    set("device",String("Firmware ")+(state_["version"]|"")+"\nFree heap: "+String(state_["freeHeap"]|0)+"\nPSRAM: "+String(ESP.getFreePsram())+"\nOTA: "+(state_["otaPhase"]|"idle")+" "+String(state_["otaProgress"]|0)+"%\n"+(state_["lastError"]|""));
    for(const char* key:{"volume","brightness"}){auto it=fields_.find(key);if(it!=fields_.end()&&!lv_obj_has_state(it->second,LV_STATE_PRESSED))lv_slider_set_value(it->second,state_[key]|0,LV_ANIM_OFF);}
    auto rotation=fields_.find("rotation");if(rotation!=fields_.end()&&!lv_dropdown_is_open(rotation->second))lv_dropdown_set_selected(rotation->second,(state_["rotation"]|0)/90);
    // Keep unsaved form edits while adopting changes made from the web UI.
    for(auto pair:{std::make_pair("ssid","ssid"),std::make_pair("host","mqttHost"),std::make_pair("username","mqttUsername")}){
        auto it=fields_.find(pair.first);if(it==fields_.end()||lv_obj_has_state(it->second,LV_STATE_FOCUSED))continue;
        const String previous=state_["previous"][pair.second]|"";String incoming=state_[pair.second]|"";
        if(String(lv_textarea_get_text(it->second))==previous)lv_textarea_set_text(it->second,incoming.c_str());
    }
}
void PanelDashboard::loop(const AppStateSnapshot& app,Snapshot snapshot,Command command,const String& overlay) {
    auto now=millis();lv_tick_inc(now-tick_);tick_=now;
    if(now-refresh_>=250){refresh_=now;JsonDocument previous;previous.set(state_);state_.clear();if(snapshot)snapshot(state_.to<JsonObject>());state_["previous"]["ssid"]=previous["ssid"];state_["previous"]["mqttHost"]=previous["mqttHost"];state_["previous"]["mqttUsername"]=previous["mqttUsername"];
        state_["name"]=app.device.friendlyName;state_["ip"]=app.network.ip;state_["wifiConnected"]=app.network.wifiConnected;state_["mqttConnected"]=app.network.mqttConnected;state_["voltage"]=app.battery.voltage;state_["title"]=app.playback.title;state_["playbackState"]=app.playback.state;state_["volume"]=app.playback.volumePercent;state_["freeHeap"]=app.system.freeHeap;state_["lastError"]=app.system.lastError;state_["otaPhase"]=app.ota.phase;state_["otaProgress"]=app.ota.progressPercent;
        lv_label_set_text(header_,(String("ELMA  ")+app.network.ip).c_str());update();
        if(overlay.length())lv_label_set_text(notice_,overlay.c_str());else if(static_cast<int32_t>(now-noticeUntil_)>=0)lv_label_set_text(notice_,"");
    }
    lv_timer_handler();
    if(!commands_.empty()){String encoded=commands_.front();commands_.pop_front();JsonDocument request;deserializeJson(request,encoded);String error;
        bool ok=command&&command(request["action"].as<String>(),request["args"],error);lv_label_set_text(notice_,ok?"Applied":error.c_str());noticeUntil_=now+4000;
    }
}
#endif
