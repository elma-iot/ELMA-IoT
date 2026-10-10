#include "round_system_dashboard.h"
#if APP_ROTARY_HMI && !APP_ROTARY_HMI_SMOKE
#include "panel_display.h"
#include "round_web_icons.h"
#include "system_metrics.h"
#include "device_log.h"
#include "version.h"
#include <WiFi.h>
#include <cmath>
#include <ctime>

namespace {
const char* keys[]={"gpio","logics","wifi","mqtt","device","oled","hardware","memory","firmware","logs","security","info"};
const char* titles[]={"Configuration","Logics","Wi-Fi","MQTT","Device","Display","Hardware Monitor","Memory / Storage","Firmware","Logs","Security","Info"};
const uint32_t colors[]={0x67caff,0xc084ff,0x60e6b0,0x55c7ff,0x8faaff,0xffd45a,0xff6681,0x6ed9ce,0x63aaff,0xb5c4d8,0xffb660,0x83d3ff};
lv_obj_t* box(lv_obj_t* parent,int x,int y,int w,int h,uint32_t color,int radius=24){
    auto* o=lv_obj_create(parent);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,radius,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;
}
lv_obj_t* label(lv_obj_t* parent,int x,int y,int w,const lv_font_t* font,uint32_t color=0xe9f3ff){
    auto* o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_font(o,font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_align(o,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_label_set_text(o,"");return o;
}
void text(lv_obj_t* o,const String& value){if(o&&value!=lv_label_get_text(o))lv_label_set_text(o,value.c_str());}
String bytes(uint64_t value){if(value>=1048576)return String(double(value)/1048576,1)+" MB";if(value>=1024)return String(double(value)/1024,1)+" KB";return String((unsigned long)value)+" B";}
String variant(JsonVariantConst value){if(value.isNull())return "Unavailable";if(value.is<const char*>())return value.as<String>();String s;serializeJson(value,s);return s;}
void icon(lv_obj_t* parent,int id,uint32_t color,int x,int y){auto* image=lv_img_create(parent);lv_img_set_src(image,RoundIcons::icons[id]);lv_obj_set_pos(image,x,y);lv_obj_set_style_img_recolor(image,lv_color_hex(color),0);lv_obj_set_style_img_recolor_opa(image,LV_OPA_COVER,0);}
void glow(void* object,int32_t opacity){lv_obj_set_style_shadow_opa(static_cast<lv_obj_t*>(object),opacity,0);}
}

void RoundSystemDashboard::begin(lv_obj_t* screen,PanelDisplay& panel,const String& raw){
    screen_=screen;panel_=&panel;JsonDocument options;deserializeJson(options,raw);
    customBackground_=!(options["systemDashboard"]|true);timeout_=constrain(options["systemTimeoutSeconds"]|60U,15U,3600U);
    security_.begin("elma-lcd-pin");enabled_=!customBackground_||security_.locked();view_=security_.locked()?Locked:Clock;lastInput_=millis();refresh_=0;clockSecond_=UINT32_MAX;dirty_=true;
    if(enabled_)draw();
}
void RoundSystemDashboard::end(){enabled_=false;screen_=nullptr;panel_=nullptr;command_=nullptr;cache_.clear();pinDraft_="";firstPin_="";oldTicket_="";}
void RoundSystemDashboard::useCustomMenu(){customBackground_=true;enabled_=security_.locked();if(enabled_){view_=Locked;dirty_=true;draw();}}
void RoundSystemDashboard::securityStatus(JsonObject out){security_.status(out);}
void RoundSystemDashboard::activity(){lastInput_=millis();notice_="";JsonDocument a,r;a["action"]="activity";security_.command(a,r.to<JsonObject>());}
void RoundSystemDashboard::message(const String& value){notice_=value.substring(0,110);text(footer_,notice_);}
bool RoundSystemDashboard::execute(const String& action,JsonVariantConst args){
    if(!command_){message("Service is starting");return false;}String error;
    if(!(*command_)(action,args,error)){message(error.length()?error:"Action unavailable");return false;}refresh_=0;return true;
}
void RoundSystemDashboard::patch(const char* path,JsonVariantConst value){JsonDocument d;auto change=d["changes"].to<JsonArray>().add<JsonObject>();change["path"]=path;change["value"].set(value);execute("patch",d.as<JsonVariantConst>());}
void RoundSystemDashboard::row(const String& name,const String& value,const String& id,uint32_t color){if(rowCount_>=int(rows_.size()))return;rows_[rowCount_++]={name.substring(0,72),value.substring(0,120),id,color};}
void RoundSystemDashboard::selectSection(int section){section_=section;selected_=metric_=0;editBrightness_=false;view_=Page;refresh_=0;dirty_=true;draw();}
void RoundSystemDashboard::clicked(lv_event_t* e){auto* self=static_cast<RoundSystemDashboard*>(lv_event_get_user_data(e));self->press();}

void RoundSystemDashboard::draw(){
    if(!enabled_||!screen_)return;
    lv_obj_clean(screen_);badges_.fill(nullptr);rowWidgets_.fill(nullptr);rowLabels_.fill(nullptr);rowValues_.fill(nullptr);
    lv_obj_set_style_bg_color(screen_,lv_color_hex(0x020409),0);lv_obj_set_style_bg_opa(screen_,LV_OPA_COVER,0);lv_obj_clear_flag(screen_,LV_OBJ_FLAG_SCROLLABLE);
    title_=label(screen_,100,65,280,&lv_font_montserrat_22);primary_=label(screen_,90,209,300,&lv_font_montserrat_48);
    subtitle_=label(screen_,90,273,300,&lv_font_montserrat_18,0xa6b8d0);footer_=label(screen_,140,409,200,&lv_font_montserrat_14,0x91a6c2);
    lv_label_set_long_mode(subtitle_,LV_LABEL_LONG_WRAP);lv_obj_set_height(subtitle_,80);lv_label_set_long_mode(footer_,LV_LABEL_LONG_WRAP);lv_obj_set_height(footer_,48);
    gauge_=lv_arc_create(screen_);lv_obj_remove_style_all(gauge_);lv_obj_set_size(gauge_,380,380);lv_obj_center(gauge_);lv_arc_set_range(gauge_,0,100);
    lv_arc_set_bg_angles(gauge_,135,45);lv_obj_set_style_arc_width(gauge_,8,LV_PART_MAIN);lv_obj_set_style_arc_width(gauge_,9,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(gauge_,lv_color_hex(0x132035),LV_PART_MAIN);lv_obj_set_style_arc_color(gauge_,lv_color_hex(0x39baff),LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(gauge_,LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_arc_opa(gauge_,LV_OPA_COVER,LV_PART_INDICATOR);lv_obj_clear_flag(gauge_,LV_OBJ_FLAG_CLICKABLE);lv_obj_move_background(gauge_);
    if(view_==Menu){
        paintedSelection_=-1;lv_obj_set_pos(footer_,140,296);lv_obj_set_height(footer_,44);lv_obj_add_flag(gauge_,LV_OBJ_FLAG_HIDDEN);lv_obj_set_pos(title_,140,212);lv_obj_set_width(title_,200);lv_obj_set_height(title_,70);lv_label_set_long_mode(title_,LV_LABEL_LONG_WRAP);
        lv_obj_add_flag(primary_,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(subtitle_,LV_OBJ_FLAG_HIDDEN);
        for(int i=0;i<12;i++){double angle=(-90.+30*i)*3.141592653589793/180.;int id=i;
            auto* badge=box(screen_,204+int(std::cos(angle)*185),204+int(std::sin(angle)*185),72,72,0x0c1420,36);badges_[i]=badge;
            lv_obj_add_flag(badge,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(badge,[](lv_event_t* e){auto* self=static_cast<RoundSystemDashboard*>(lv_event_get_user_data(e));auto* target=lv_event_get_target(e);for(int j=0;j<12;j++)if(self->badges_[j]==target){self->selected_=j;self->press();break;}},LV_EVENT_CLICKED,this);
            icon(badge,id,colors[id],12,12);
        }
    }else if(view_==Page){
        text(title_,titles[section_]);icon(screen_,section_,colors[section_],216,103);
        if(section_!=5&&section_!=6&&section_!=7&&section_!=8){
            lv_obj_add_flag(gauge_,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(primary_,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(subtitle_,LV_OBJ_FLAG_HIDDEN);
            for(int i=0;i<3;i++){rowWidgets_[i]=box(screen_,74,161+i*76,332,68,0x0c1420,23);rowLabels_[i]=label(rowWidgets_[i],14,9,304,&lv_font_montserrat_14,0xa6b8d0);rowValues_[i]=label(rowWidgets_[i],14,29,304,&lv_font_montserrat_18);if(section_==9){lv_obj_set_style_text_font(rowValues_[i],&lv_font_montserrat_14,0);lv_obj_set_pos(rowValues_[i],14,25);lv_label_set_long_mode(rowValues_[i],LV_LABEL_LONG_WRAP);lv_obj_set_height(rowValues_[i],39);}}
        }else{lv_obj_set_size(gauge_,312,312);lv_obj_set_pos(gauge_,84,103);lv_obj_set_pos(primary_,90,208);}
        if(section_==0)drawWiring();
    }else if(view_==Wiring){
        text(title_,"Configured circuit");lv_obj_add_flag(gauge_,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(primary_,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(subtitle_,LV_OBJ_FLAG_HIDDEN);drawWiring();
    }else if(view_==Pin){
        text(title_,pinStage_==0?"Enter current PIN":pinStage_==1?"Set a new PIN":pinStage_==2?"Confirm new PIN":"Unlock display");
        lv_obj_set_style_arc_color(gauge_,lv_color_hex(colors[10]),LV_PART_INDICATOR);
    }else if(view_==Locked){
        icon(screen_,10,colors[10],216,147);text(title_,"Display locked");text(primary_,LV_SYMBOL_CLOSE);text(subtitle_,"Turn or press to unlock");
    }else if(view_==Confirm){text(title_,"Install firmware?");text(subtitle_,"Uses the existing OTA updater");}
    else{text(title_,live_.device.friendlyName.length()?live_.device.friendlyName:"ELMA IoT");}
    lv_obj_remove_event_cb(screen_,clicked);
    if(view_!=Locked)lv_obj_add_event_cb(screen_,clicked,LV_EVENT_CLICKED,this);
    dirty_=false;update();
}

void RoundSystemDashboard::refreshRows(){
    rowCount_=0;auto metrics=getSystemMetricsSnapshot();JsonVariantConst config=cache_["settings"];
    switch(section_){
    case 0:
        row("Circuit overview","Press for wiring diagram","$diagram");row("Board",metrics.hardware.boardProfile);
        for(JsonPairConst slot:config["ui"]["peripheralHelperBindings"].as<JsonObjectConst>())for(JsonPairConst pin:slot.value().as<JsonObjectConst>()){
            String name=pin.key().c_str();if(pin.value().is<int>()&&!name.startsWith("LED_")&&name!="I2C_ADDRESS")row(String(slot.key().c_str())+" / "+name,"GPIO "+variant(pin.value()));
        }
        if(rowCount_==2)row("Circuit","No external peripherals configured");
        break;
    case 1:
        row("All workflows",cache_["logics"]["mode"]|"stopped","$all",0xc084ff);
        for(JsonObjectConst group:cache_["logics"]["groups"].as<JsonArrayConst>())row(group["name"]|group["id"]|"Workflow",group["mode"]|"playing",group["id"]|"");
        if(rowCount_==1)row("Workflows","No saved workflow groups");
        if(!cache_["logics"]["live"]["error"].isNull())row("Execution",variant(cache_["logics"]["live"]["error"]),"",0xff6681);
        break;
    case 2:{
        auto mode=WiFi.getMode();row("Mode",mode==WIFI_AP_STA?"AP + STA":mode==WIFI_AP?"AP":mode==WIFI_STA?"STA":"Off");
        row("Station",live_.network.wifiConnected?"Connected":"Disconnected","",live_.network.wifiConnected?0x60e6b0:0xff6681);
        row("SSID",live_.network.ssid.length()?live_.network.ssid:"Not connected");row("Station IP",live_.network.ip);
        if(mode==WIFI_AP||mode==WIFI_AP_STA)row("Access point",WiFi.softAPIP().toString());
        row("Signal",live_.network.wifiConnected?String(live_.network.wifiRssi)+" dBm":"Unavailable");break;}
    case 3:
        row("Connection",live_.network.mqttConnected?"Connected":"Disconnected","",live_.network.mqttConnected?0x60e6b0:0xff6681);
        row("Broker",variant(config["mqtt"]["host"]));row("Port",variant(config["mqtt"]["port"]));row("Client ID",variant(config["mqtt"]["clientId"]));
        row("Last system error",live_.system.lastError.length()?live_.system.lastError:"None reported");break;
    case 4:
        row("Device",live_.device.friendlyName);row("Identifier",live_.device.deviceName);row("Playback",live_.playback.state);row("Title",live_.playback.title);
        if(live_.input.button1.configuredIndex>=0)row("Input 1",live_.input.button1.active?"Active":"Idle");
        if(live_.input.button2.configuredIndex>=0)row("Input 2",live_.input.button2.active?"Active":"Idle");
        if(cache_["caps"]["battery"]==true)row("Battery",String(live_.battery.voltage,2)+" V");
        row("Last error",live_.system.lastError.length()?live_.system.lastError:"None");break;
    case 5:
        if(!brightnessPending_)brightness_=config["oled"]["brightness"]|brightness_;
        row("Auto screen off",String(config["oled"]["dimTimeoutSeconds"]|0)+" seconds");break;
    case 7:
        row("Internal RAM",bytes(metrics.sram.freeBytes)+" free");
        if(metrics.psram.available)row("PSRAM",bytes(metrics.psram.freeBytes)+" free");
        row("Flash",bytes(metrics.hardware.flashSizeBytes));
        if(metrics.spiffs.available)row("Filesystem",metrics.spiffs.mounted?bytes(metrics.spiffs.freeBytes)+" free":"Not mounted");
        if(metrics.sd.available)row("SD card",metrics.sd.mounted?bytes(metrics.sd.freeBytes)+" free":"Not mounted");
        break;
    case 8:
        row("Installed",APP_VERSION);row("Update",live_.ota.updateAvailable?live_.ota.latestVersion:"No update known");row("Result",live_.ota.lastError.length()?live_.ota.lastError:live_.ota.lastResult);
        row("Check updates","Press to check","$check");if(live_.ota.updateAvailable)row("Install update",live_.ota.latestVersion,"$install");break;
    case 10:{
        JsonDocument s;securityStatus(s.to<JsonObject>());bool on=s["enabled"]|false;
        row("Display PIN",on?"Enabled":"Not set");row(on?"Change PIN":"Set PIN","Press to enter digits",on?"change":"set");
        if(on){row("Remove PIN","Current PIN required","disable");row("Lock display","Press to lock","lock");}
        row("Home timeout",String(timeout_)+" seconds","$timeout");break;}
    case 11:
        row("Device name",live_.device.friendlyName);row("Identifier",live_.device.deviceName);row("Board",metrics.hardware.boardProfile);row("Chip",metrics.hardware.chipModel);
        row("Revision",String(metrics.hardware.chipRevision));row("Firmware",APP_VERSION);row("Build",__DATE__ " " __TIME__);row("CPU",String(metrics.hardware.cpuFreqMHz)+" MHz");
        row("Uptime",String(millis()/1000)+" seconds");row("MAC address",WiFi.macAddress());break;
    }
    selected_=constrain(selected_,0,max(0,rowCount_-1));
}

void RoundSystemDashboard::paintRows(){
    if(section_==9){
        unsigned first=logCount_>3?constrain(logSelected_-1,0,int(logCount_)-3):0;
        for(int i=0;i<3;i++){unsigned index=first+i;if(index>=logCount_){lv_obj_add_flag(rowWidgets_[i],LV_OBJ_FLAG_HIDDEN);continue;}
            lv_obj_clear_flag(rowWidgets_[i],LV_OBJ_FLAG_HIDDEN);text(rowLabels_[i],String(index+1)+" / "+String(logCount_));
            const char* begin=logs_+logOffsets_[index];const char* end=strchr(begin,'\n');String line(begin,end?size_t(end-begin):strlen(begin));
            text(rowValues_[i],line.substring(0,110));lv_obj_set_style_border_width(rowWidgets_[i],int(index)==logSelected_?2:0,0);lv_obj_set_style_border_color(rowWidgets_[i],lv_color_hex(0x39baff),0);
        }
        return;
    }
    int first=rowCount_>3?constrain(selected_-1,0,rowCount_-3):0;
    for(int i=0;i<3;i++){if(!rowWidgets_[i])continue;int index=first+i;if(index>=rowCount_){lv_obj_add_flag(rowWidgets_[i],LV_OBJ_FLAG_HIDDEN);continue;}
        lv_obj_clear_flag(rowWidgets_[i],LV_OBJ_FLAG_HIDDEN);text(rowLabels_[i],rows_[index].label);text(rowValues_[i],rows_[index].value);
        lv_obj_set_style_text_color(rowValues_[i],lv_color_hex(rows_[index].color),0);lv_obj_set_style_border_color(rowWidgets_[i],lv_color_hex(colors[section_]),0);lv_obj_set_style_border_width(rowWidgets_[i],index==selected_?2:0,0);
    }
}

void RoundSystemDashboard::updateGauge(){
    auto m=getSystemMetricsSnapshot();String name,value,hint;double percent=0;
    if(section_==5){name="Brightness";value=String(brightness_)+"%";percent=brightness_;hint=editBrightness_?"Turn to adjust\nPress to save":"Press to adjust";}
    else if(section_==6){
        int n=(metric_%4+4)%4;
        if(n==0){name="CPU load";value=m.cpuLoadAvailable?String(m.cpuLoadPercent)+"%":"--";percent=m.cpuLoadPercent;}
        else if(n<3){name=String("CPU core ")+String(n);bool available=m.cpuLoadAvailable&&n<=m.cpuLoadCoreCount;value=available?String(m.cpuLoadCorePercent[n-1])+"%":"--";percent=available?m.cpuLoadCorePercent[n-1]:0;}
        else{name="Chip temperature";value=m.chipTemperatureAvailable?String(m.chipTemperatureC,1)+" C":"--";percent=m.chipTemperatureAvailable?constrain((m.chipTemperatureC+20)*100/120,0.f,100.f):0;}
        hint="Live hardware monitor\nTurn for next metric";
    }else if(section_==7){
        std::array<const ResourceMetricSnapshot*,4> resources={{&m.sram,&m.psram,&m.spiffs,&m.sd}};const char* labels[]={"Internal RAM","PSRAM","Filesystem","SD card"};
        int count=0;std::array<int,5> available{};for(int i=0;i<4;i++)if(resources[i]->available)available[count++]=i;available[count++]=4;
        int index=available[(metric_%count+count)%count];
        if(index==4){name="Flash / application";value=bytes(m.hardware.sketchSizeBytes);hint=bytes(m.hardware.flashSizeBytes)+" installed\nApp slot "+bytes(m.hardware.appPartitionSizeBytes);percent=m.hardware.appPartitionSizeBytes?100.*m.hardware.sketchSizeBytes/m.hardware.appPartitionSizeBytes:0;}
        else{const auto& r=*resources[index];name=labels[index];value=r.mounted?bytes(r.usedBytes):"Not mounted";hint=bytes(r.freeBytes)+" free / "+bytes(r.totalBytes)+" total";percent=r.mounted&&r.totalBytes?100.*r.usedBytes/r.totalBytes:0;}
    }else if(section_==8){
        name=live_.ota.busy?live_.ota.phase:rows_[selected_].label;value=live_.ota.busy?String(live_.ota.progressPercent)+"%":selected_>=3?LV_SYMBOL_DOWNLOAD:LV_SYMBOL_OK;
        hint=live_.ota.busy?"Update in progress":rows_[selected_].value;percent=live_.ota.busy?live_.ota.progressPercent:0;
    }
    text(title_,name);text(primary_,value);text(subtitle_,hint);lv_obj_set_style_arc_color(gauge_,lv_color_hex(colors[section_]),LV_PART_INDICATOR);lv_arc_set_value(gauge_,constrain(int(percent),0,100));
}

void RoundSystemDashboard::update(){
    if(!enabled_)return;
    if(view_==Clock){text(title_,live_.device.friendlyName.length()?live_.device.friendlyName:"ELMA IoT");time_t now=time(nullptr);const bool synced=now>1577836800;now+=live_.device.clockUtcOffsetMinutes*60;tm utc{};char timeText[12]="--:--",date[36]="Waiting for time sync";
        if(synced&&gmtime_r(&now,&utc)){strftime(timeText,sizeof(timeText),"%H:%M",&utc);strftime(date,sizeof(date),"%a, %d %b %Y",&utc);lv_arc_set_value(gauge_,utc.tm_sec*100/59);}
        text(primary_,timeText);char zone[16];const int offset=live_.device.clockUtcOffsetMinutes;snprintf(zone,sizeof(zone),"UTC%c%02d:%02d",offset<0?'-':'+',abs(offset)/60,abs(offset)%60);text(subtitle_,String(zone)+"\n"+date);text(footer_,String(live_.network.wifiConnected?LV_SYMBOL_WIFI " Connected":"Wi-Fi offline")+"\nMQTT "+(live_.network.mqttConnected?"connected":"offline"));
    }else if(view_==Menu){if(paintedSelection_==selected_)return;text(title_,titles[selected_]);text(footer_,String(selected_+1)+" / 12\nPress to open · hold for clock");
        for(int i=0;i<12;i++){int id=i;bool on=id==selected_;lv_obj_set_style_border_width(badges_[i],on?3:1,0);lv_obj_set_style_border_color(badges_[i],lv_color_hex(colors[id]),0);
            lv_obj_set_style_shadow_color(badges_[i],lv_color_hex(colors[id]),0);lv_obj_set_style_shadow_width(badges_[i],on?18:8,0);lv_obj_set_style_shadow_opa(badges_[i],on?LV_OPA_50:LV_OPA_20,0);lv_obj_set_style_bg_color(badges_[i],lv_color_hex(on?0x15243a:0x09101b),0);
            if(paintedSelection_!=selected_){lv_anim_del(badges_[i],glow);if(on){lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,badges_[i]);lv_anim_set_exec_cb(&a,glow);lv_anim_set_values(&a,LV_OPA_20,LV_OPA_50);lv_anim_set_time(&a,150);lv_anim_start(&a);}}
        }paintedSelection_=selected_;
    }else if(view_==Page){
        if(section_==5||section_==6||section_==7||section_==8)updateGauge();else paintRows();
        text(footer_,notice_.length()?notice_:section_==9?String(followLogs_?"Following latest":"History")+" · press for latest\nHold to go back":String("Turn to browse · hold to go back"));
    }else if(view_==Confirm){text(primary_,confirmation_?"Update":"Cancel");text(footer_,"Turn to choose · press to confirm");lv_arc_set_value(gauge_,confirmation_?100:0);}
    else if(view_==Pin){text(primary_,pinDigit_==10?LV_SYMBOL_BACKSPACE:String(pinDigit_));String dots;for(unsigned i=0;i<4;i++)dots+=i<pinDraft_.length()?"* ":"_ ";text(subtitle_,dots);text(footer_,notice_.length()?notice_:"Turn for digit / backspace\nPress to confirm · hold to cancel");lv_arc_set_value(gauge_,pinDraft_.length()*25);}
}

void RoundSystemDashboard::loop(const AppStateSnapshot& live,const PanelDashboard::Snapshot& snapshot,const PanelDashboard::Command& command){
    if(!screen_)return;
    live_=live;command_=&command;security_.tick();uint32_t now=millis();
    if(security_.locked()&&!enabled_){enabled_=true;view_=Locked;dirty_=true;}
    if(customBackground_&&!security_.locked()&&view_!=Pin){enabled_=false;return;}
    if(brightnessPending_&&now-brightnessChanged_>=250){JsonDocument v;v.set(brightness_);brightnessPending_=false;patch("oled/brightness",v.as<JsonVariantConst>());}
    if(security_.locked()&&view_!=Locked&&view_!=Pin){view_=Locked;dirty_=true;pinDraft_="";firstPin_="";}
    if(view_!=Clock&&view_!=Locked&&!live_.ota.busy&&now-lastInput_>=timeout_*1000UL){
        // Brightness is committed as it is adjusted; PIN drafts are discarded.
        editBrightness_=false;pinDraft_="";firstPin_="";oldTicket_="";view_=security_.locked()?Locked:Clock;selected_=0;dirty_=true;
    }
    bool refreshed=false;
    if((view_==Page||view_==Wiring)&&(!refresh_||now-refresh_>=1000)){
        refreshed=true;
        refresh_=now;cache_.clear();if(snapshot){snapshot(String("round/")+keys[section_],cache_.to<JsonObject>());}
        refreshRows();
        if(view_==Wiring){String signature;serializeJson(cache_["settings"]["ui"]["peripheralHelperBindings"],signature);if(signature!=wiringSignature_){wiringSignature_=signature;dirty_=true;}}
        if(section_==9){uint64_t sequence;size_t length=DebugLog.readTail(logs_,sizeof(logs_),sequence);
            if(sequence!=logSequence_){logSequence_=sequence;logCount_=0;
                unsigned start=length==4096?(strchr(logs_,'\n')?unsigned(strchr(logs_,'\n')-logs_)+1:0):0;
                for(unsigned p=start;p<length;p++){if(p==start||logs_[p-1]=='\n'){if(logCount_==50){memmove(logOffsets_,logOffsets_+1,49*sizeof(uint16_t));logCount_=49;}logOffsets_[logCount_++]=p;}}
                if(followLogs_)logSelected_=max(0,int(logCount_)-1);else logSelected_=constrain(logSelected_,0,max(0,int(logCount_)-1));
            }
        }
    }
    if(dirty_)draw();
    if(clockSecond_!=now/1000||refreshed){clockSecond_=now/1000;update();}
}

void RoundSystemDashboard::rotate(int delta){
    if(!enabled_||!delta)return;
    activity();
    if(view_==Locked){pinStart("unlock",3);return;}
    if(view_==Clock){view_=Menu;selected_=delta>0?0:11;dirty_=true;}
    else if(view_==Menu){selected_=(selected_+delta%12+12)%12;}
    else if(view_==Pin)pinDigit_=(pinDigit_+delta%11+11)%11;
    else if(view_==Confirm)confirmation_=delta>0;
    else if(view_==Wiring){int count=cache_["settings"]["ui"]["peripheralHelperBindings"].size();metric_=constrain(metric_+delta,0,max(0,(count-1)/6));dirty_=true;}
    else if(view_==Page){
        if(section_==5&&editBrightness_){brightness_=constrain(brightness_+delta*2,1,100);panel_->brightness(brightness_);brightnessPending_=true;brightnessChanged_=millis();}
        else if(section_==6||section_==7)metric_+=delta;
        else if(section_==9){followLogs_=false;logSelected_=constrain(logSelected_+delta,0,max(0,int(logCount_)-1));}
        else {int multiplier=millis()-lastRotation_<75&&rowCount_>12?3:1;selected_=constrain(selected_+delta*multiplier,0,max(0,rowCount_-1));}
    }
    lastRotation_=millis();if(dirty_)draw();else update();
}
void RoundSystemDashboard::press(){
    if(!enabled_)return;
    activity();
    if(view_==Locked){pinStart("unlock",3);return;}
    if(view_==Clock){view_=Menu;selected_=0;dirty_=true;}
    else if(view_==Menu){selectSection(selected_);return;}
    else if(view_==Pin){if(pinDigit_==10){if(pinDraft_.length())pinDraft_.remove(pinDraft_.length()-1);}else{pinDraft_+=char('0'+pinDigit_);if(pinDraft_.length()==4){pinSubmit();return;}}}
    else if(view_==Wiring){view_=Page;dirty_=true;}
    else if(view_==Confirm){view_=Page;dirty_=true;if(confirmation_){JsonDocument args;execute("otaInstall",args.as<JsonVariantConst>());}}
    else if(view_==Page){
        if(section_==5){editBrightness_=!editBrightness_;if(brightnessPending_){JsonDocument v;v.set(brightness_);brightnessPending_=false;patch("oled/brightness",v.as<JsonVariantConst>());}}
        else if(section_==9){followLogs_=true;logSelected_=max(0,int(logCount_)-1);}
        else if(rowCount_){const Row selected=rows_[selected_];JsonDocument args;
            if(section_==0&&selected.id=="$diagram"){view_=Wiring;metric_=0;dirty_=true;}
            else if(section_==1&&selected.id.length()){
                const char* mode=selected.value=="playing"?"paused":"playing";
                if(selected.id=="$all")args["mode"]=mode;else{args["group"]["id"]=selected.id;args["group"]["mode"]=mode;}execute("logics",args.as<JsonVariantConst>());
            }else if(section_==8&&!live_.ota.busy){if(selected.id=="$check")execute("otaCheck",args.as<JsonVariantConst>());else if(selected.id=="$install"){view_=Confirm;confirmation_=false;dirty_=true;}}
            else if(section_==10){
                if(selected.id=="$timeout"){timeout_=timeout_==60?120:timeout_==120?300:60;JsonDocument config;deserializeJson(config,cache_["settings"]["oled"]["circularMenu"]|"{}");if(!config["items"].is<JsonArrayConst>()){config["schemaVersion"]=1;auto home=config["items"].to<JsonArray>().add<JsonObject>();home["id"]=1;home["title"]="Home";home["kind"]="text";}config["systemTimeoutSeconds"]=timeout_;config["systemDashboard"]=true;String raw;serializeJson(config,raw);JsonDocument value;value.set(raw);patch("oled/circularMenu",value.as<JsonVariantConst>());refreshRows();}
                else if(selected.id=="lock"){args["action"]="lock";JsonDocument result;security_.command(args,result.to<JsonObject>());if(result["ok"]==true){view_=Locked;dirty_=true;}else message(result["error"]|"Unable to lock");}
                else if(selected.id=="set")pinStart("set",1);else if(selected.id=="change"||selected.id=="disable")pinStart(selected.id,0);
            }
        }
    }
    if(dirty_)draw();else update();
}
void RoundSystemDashboard::back(){activity();if(view_==Locked)return;if(view_==Pin){pinDraft_="";firstPin_="";oldTicket_="";view_=security_.locked()?Locked:Page;section_=10;selected_=0;refresh_=0;}
    else if(view_==Confirm||view_==Wiring)view_=Page;else if(view_==Page){editBrightness_=false;view_=Menu;selected_=section_;}else view_=Clock;
    dirty_=true;draw();}
void RoundSystemDashboard::pinStart(const String& action,int stage){pinAction_=action;pinStage_=stage;pinDraft_="";firstPin_="";oldTicket_="";pinDigit_=0;view_=Pin;dirty_=true;draw();}
void RoundSystemDashboard::pinSubmit(){
    JsonDocument args,result;
    if(pinStage_==0){args["action"]=pinAction_=="disable"?"disable":"verify";args["pin"]=pinDraft_;int status=security_.command(args,result.to<JsonObject>());pinDraft_="";
        if(status!=200){message(result["error"]|"PIN rejected");update();return;}
        if(pinAction_=="disable"){view_=Page;section_=10;refresh_=0;dirty_=true;draw();return;}oldTicket_=result["ticket"]|"";pinStage_=1;dirty_=true;
    }else if(pinStage_==1){firstPin_=pinDraft_;pinDraft_="";pinStage_=2;dirty_=true;}
    else if(pinStage_==2){
        args["action"]=pinAction_;args["ticket"]=oldTicket_;args["newPin"]=firstPin_;args["confirmPin"]=pinDraft_;args["timeoutSeconds"]=300;
        int status=security_.command(args,result.to<JsonObject>());pinDraft_="";firstPin_="";oldTicket_="";
        if(status!=200){pinStage_=pinAction_=="change"?0:1;message(result["error"]|"PINs did not match");dirty_=true;}else{view_=Page;section_=10;refresh_=0;dirty_=true;}
    }else{args["action"]="unlock";args["pin"]=pinDraft_;int status=security_.command(args,result.to<JsonObject>());pinDraft_="";if(status==200){view_=Clock;dirty_=true;}else message(result["error"]|"Incorrect PIN");}
    if(dirty_)draw();else update();
}

void RoundSystemDashboard::drawWiring(){
    if(view_!=Wiring)return;
    auto* board=box(screen_,185,199,110,82,0x18334d,18);text(label(board,2,19,106,&lv_font_montserrat_14),"ESP32-S3\nGPIO");
    auto bindings=cache_["settings"]["ui"]["peripheralHelperBindings"].as<JsonObjectConst>();
    int total=bindings.size(),first=min(metric_*6,max(0,((total-1)/6)*6)),index=0,visible=0,lineIndex=0;
    for(JsonPairConst slot:bindings){if(index++<first)continue;if(visible>=6)break;
        double angle=(-90.+60*visible)*3.141592653589793/180.;int cx=240+int(std::cos(angle)*145),cy=240+int(std::sin(angle)*145);++visible;
        String pins;int pinCount=0;
        for(JsonPairConst pin:slot.value().as<JsonObjectConst>()){
            String role=pin.key().c_str();if(!pin.value().is<int>()||role.startsWith("LED_")||role=="I2C_ADDRESS")continue;
            if(pinCount<2)pins+=role+" "+variant(pin.value())+" ";
            ++pinCount;
            if(lineIndex<18){uint32_t ink=0x0f766e;role.toUpperCase();for(const auto& wire:RoundIcons::wireColors)if(role==wire.signal){ink=wire.color;break;}
                auto& points=wires_[lineIndex++];int offset=(pinCount%3-1)*5;points={{{int16_t(240+std::cos(angle)*55),int16_t(240+std::sin(angle)*42+offset)},{int16_t(cx),int16_t(240+std::sin(angle)*42+offset)},{int16_t(cx),int16_t(cy)}}};
                auto* wire=lv_line_create(screen_);lv_line_set_points(wire,points.data(),3);lv_obj_set_style_line_color(wire,lv_color_hex(ink),0);lv_obj_set_style_line_width(wire,2,0);lv_obj_move_background(wire);
            }
        }
        auto* peripheral=box(screen_,cx-48,cy-25,96,50,0x101f30,14);auto* name=label(peripheral,3,5,90,&lv_font_montserrat_14);text(name,slot.key().c_str());text(label(peripheral,3,27,90,&lv_font_montserrat_14,0x91c9e8),pins.length()?pins:"No GPIO binding");
    }
    text(footer_,total?String("Connections ")+String(first+1)+"–"+String(min(first+6,total))+" / "+String(total)+"\nTurn to pan · press for pin details":"No external circuit configured\nPress for pin details");
}
#endif
