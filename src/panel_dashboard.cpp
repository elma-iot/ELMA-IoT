#include "panel_dashboard.h"
#if APP_HAS_ONBOARD_PANEL
#include "panel_display.h"
#include "panel_settings.h"
#include "panel_forms.h"
#include "device_log.h"
#include <ctime>
#include <cmath>
namespace {
struct Tab {const char* key;const char* title;bool conditional;};
const Tab tabList[]={
 {"gpio","Configuration",false},{"logics","Logics",false},{"wled","WLED",true},{"motor","Motor",true},
 {"playback","Audio",true},{"effects","Audio Effects",true},{"wifi","Wi-Fi",false},{"mqtt","MQTT",false},
 {"battery","Battery",true},{"device","Device",false},{"oled","DISPLAY",true},{"hardware","Hardware Monitor",false},
 {"storage-internal","Internal Storage",false},{"storage-external","External Storage",true},{"migration","Import Device",true},
 {"firmware","Firmware",false},{"logs","Logs",false},{"plots","Plots",true},{"security","Security",false},{"info","Info",false}};
String valueText(JsonVariantConst v){if(v.is<const char*>())return v.as<String>();String out;serializeJson(v,out);return v.isNull()?String(""):out;}
String itemAt(const String& options,int index){int start=0;while(index-->0){int next=options.indexOf('\n',start);if(next<0)return "";start=next+1;}int end=options.indexOf('\n',start);return options.substring(start,end<0?options.length():end);}
}
PanelDashboard::~PanelDashboard(){if(input_)lv_indev_delete(input_);if(display_)lv_disp_remove(display_);if(pixels_)heap_caps_free(pixels_);}
bool PanelDashboard::begin(uint8_t rotation){
 static bool initialized=false;if(!initialized){lv_init();initialized=true;}
 pixels_=static_cast<lv_color_t*>(heap_caps_malloc(kPanelWidth*20*sizeof(lv_color_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));if(!pixels_)return false;
 lv_disp_draw_buf_init(&drawBuffer_,pixels_,nullptr,kPanelWidth*20);lv_disp_drv_init(&displayDriver_);
 displayDriver_.hor_res=kPanelWidth;displayDriver_.ver_res=kPanelHeight;displayDriver_.draw_buf=&drawBuffer_;displayDriver_.flush_cb=flush;displayDriver_.user_data=this;displayDriver_.sw_rotate=1;
 display_=lv_disp_drv_register(&displayDriver_);if(!display_)return false;lv_disp_set_rotation(display_,static_cast<lv_disp_rot_t>(rotation));
 lv_indev_drv_init(&inputDriver_);inputDriver_.type=LV_INDEV_TYPE_POINTER;inputDriver_.read_cb=touch;inputDriver_.user_data=this;inputDriver_.disp=display_;input_=lv_indev_drv_register(&inputDriver_);
 screen_=lv_disp_get_scr_act(display_);lv_obj_set_style_bg_color(screen_,lv_color_hex(0x111827),0);lv_obj_set_style_text_color(screen_,lv_color_hex(0xf3f4f6),0);lv_obj_set_style_pad_all(screen_,6,0);lv_obj_set_style_pad_row(screen_,5,0);lv_obj_set_flex_flow(screen_,LV_FLEX_FLOW_COLUMN);lv_obj_clear_flag(screen_,LV_OBJ_FLAG_SCROLLABLE);
 auto* bar=lv_obj_create(screen_);lv_obj_set_style_text_color(bar,lv_color_hex(0xf3f4f6),0);lv_obj_set_size(bar,LV_PCT(100),28);lv_obj_set_style_pad_all(bar,0,0);lv_obj_set_style_border_width(bar,0,0);lv_obj_set_style_bg_opa(bar,LV_OPA_TRANSP,0);lv_obj_clear_flag(bar,LV_OBJ_FLAG_SCROLLABLE);
 wifi_=lv_label_create(bar);lv_obj_set_pos(wifi_,0,5);lv_label_set_text(wifi_,LV_SYMBOL_WIFI " --");
 for(int n=0;n<4;n++){bars_[n]=lv_obj_create(bar);lv_obj_set_size(bars_[n],3,4+n*4);lv_obj_set_pos(bars_[n],63+n*5,21-(4+n*4));lv_obj_set_style_border_width(bars_[n],0,0);lv_obj_set_style_radius(bars_[n],0,0);}
 mqtt_=lv_label_create(bar);lv_obj_set_pos(mqtt_,87,5);extra_=lv_label_create(bar);lv_obj_set_pos(extra_,170,5);clock_=lv_label_create(bar);lv_obj_align(clock_,LV_ALIGN_RIGHT_MID,0,0);
 menu_=lv_dropdown_create(screen_);lv_obj_set_size(menu_,LV_PCT(100),44);lv_obj_add_event_cb(menu_,[](lv_event_t* e){auto* s=static_cast<PanelDashboard*>(lv_event_get_user_data(e));auto i=lv_dropdown_get_selected(s->menu_);if(i<s->tabs_.size())s->page(s->tabs_[i]);},LV_EVENT_VALUE_CHANGED,this);
 body_=lv_obj_create(screen_);lv_obj_set_width(body_,LV_PCT(100));lv_obj_set_flex_grow(body_,1);lv_obj_set_flex_flow(body_,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_all(body_,8,0);lv_obj_set_style_pad_row(body_,9,0);lv_obj_set_style_bg_color(body_,lv_color_hex(0x1f2937),0);lv_obj_set_style_text_color(body_,lv_color_hex(0xf3f4f6),0);
 notice_=lv_label_create(screen_);lv_obj_set_width(notice_,LV_PCT(100));lv_label_set_long_mode(notice_,LV_LABEL_LONG_WRAP);lv_label_set_text(notice_,"");
 keyboard_=lv_keyboard_create(lv_disp_get_layer_top(display_));lv_obj_set_height(keyboard_,LV_PCT(48));lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);
 lv_obj_add_event_cb(keyboard_,[](lv_event_t* e){if(lv_event_get_code(e)==LV_EVENT_READY||lv_event_get_code(e)==LV_EVENT_CANCEL){auto* s=static_cast<PanelDashboard*>(lv_event_get_user_data(e));lv_keyboard_set_textarea(s->keyboard_,nullptr);lv_obj_add_flag(s->keyboard_,LV_OBJ_FLAG_HIDDEN);lv_obj_set_height(s->screen_,lv_disp_get_ver_res(s->display_));}},LV_EVENT_ALL,this);
 tick_=millis();syncMenu();page("gpio");return true;
}
void PanelDashboard::flush(lv_disp_drv_t* d,const lv_area_t* a,lv_color_t* c){auto* s=static_cast<PanelDashboard*>(d->user_data);bool ok=s->panel_.drawColor(a->x1,a->y1,a->x2-a->x1+1,a->y2-a->y1+1,reinterpret_cast<uint8_t*>(c));if(!s->frameReported_){DebugLog.printf("[display] Portrait web tabs ready; LVGL flush %s\n",ok?"completed":"FAILED");s->frameReported_=true;}lv_disp_flush_ready(d);}
void PanelDashboard::touch(lv_indev_drv_t* d,lv_indev_data_t* data){auto* s=static_cast<PanelDashboard*>(d->user_data);int16_t x,y;if(s->panel_.readRawTouch(x,y)){data->point.x=x;data->point.y=y;data->state=LV_INDEV_STATE_PRESSED;s->touched_=true;}else data->state=LV_INDEV_STATE_RELEASED;}
lv_obj_t* PanelDashboard::label(const String& text){auto* o=lv_label_create(body_);lv_obj_set_width(o,LV_PCT(100));lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);lv_label_set_text(o,text.c_str());return o;}
void PanelDashboard::section(const String& text){auto* o=label(text);lv_obj_set_style_text_color(o,lv_color_hex(0xf59e0b),0);}
void PanelDashboard::button(const String& caption,const String& key){auto* o=lv_btn_create(body_);lv_obj_set_size(o,LV_PCT(100),44);auto* t=lv_label_create(o);lv_obj_set_width(t,LV_PCT(95));lv_label_set_long_mode(t,LV_LABEL_LONG_DOT);lv_label_set_text(t,caption.c_str());lv_obj_center(t);buttons_[key]=o;lv_obj_add_event_cb(o,event,LV_EVENT_CLICKED,this);}
void PanelDashboard::field(const String& caption,const String& path,const String& kind,const String& options,double minimum,double maximum){
 label(caption);Field f;f.path=path;f.kind=kind;f.options=options;f.minimum=minimum;f.maximum=maximum;f.secret=kind=="secret";
 if(kind=="switch"){f.object=lv_switch_create(body_);lv_obj_set_size(f.object,56,30);}
 else if(kind=="select"){f.object=lv_dropdown_create(body_);lv_obj_set_size(f.object,LV_PCT(100),44);lv_dropdown_set_options(f.object,options.c_str());}
 else if(kind=="slider"){f.object=lv_slider_create(body_);lv_obj_set_size(f.object,LV_PCT(94),24);lv_slider_set_range(f.object,minimum,maximum);lv_obj_set_ext_click_area(f.object,10);}
 else{f.object=lv_textarea_create(body_);lv_obj_set_size(f.object,LV_PCT(100),44);lv_textarea_set_one_line(f.object,true);lv_textarea_set_max_length(f.object,f.secret?64:192);lv_textarea_set_password_mode(f.object,f.secret);if(kind=="number")lv_textarea_set_accepted_chars(f.object,"0123456789.-");}
 if(kind!="slider"&&kind!="switch"){lv_obj_set_style_bg_color(f.object,lv_color_hex(0x111827),LV_PART_MAIN);lv_obj_set_style_text_color(f.object,lv_color_hex(0xf3f4f6),LV_PART_MAIN);}
 auto draft=drafts_.find(path);
 if(draft!=drafts_.end()){
  f.dirty=true;const String& text=draft->second;
  if(kind=="switch"){if(text=="true")lv_obj_add_state(f.object,LV_STATE_CHECKED);}
  else if(kind=="slider")lv_slider_set_value(f.object,text.toInt(),LV_ANIM_OFF);
  else if(kind=="select"){for(int i=0;i<lv_dropdown_get_option_cnt(f.object);i++)if(itemAt(options,i)==text){lv_dropdown_set_selected(f.object,i);break;}}
  else lv_textarea_set_text(f.object,text.c_str());
 }
 lv_obj_add_event_cb(f.object,event,LV_EVENT_ALL,this);fields_.push_back(f);
}
String PanelDashboard::fieldText(const Field& f)const{if(f.kind=="switch")return lv_obj_has_state(f.object,LV_STATE_CHECKED)?"true":"false";if(f.kind=="slider")return String(lv_slider_get_value(f.object));if(f.kind=="select")return itemAt(f.options,lv_dropdown_get_selected(f.object));return lv_textarea_get_text(f.object);}
void PanelDashboard::settingsFields(){for(const auto& f:panelFormFields)if(page_==f.page)field(f.caption,f.path,f.kind,f.options,f.minimum,f.maximum);}
void PanelDashboard::syncMenu(){
 String options,signature;std::vector<String> next;bool locked=state_["security"]["locked"]|false;
 for(const auto& tab:tabList)if((!locked||String(tab.key)=="security")&&(!tab.conditional||state_["caps"][tab.key]==true)){if(options.length())options+='\n';options+=tab.title;signature+=String(tab.key)+"|";next.emplace_back(tab.key);}
 if(signature==menuSignature_)return;menuSignature_=signature;tabs_=next;lv_dropdown_set_options(menu_,options.c_str());bool found=false;for(size_t i=0;i<tabs_.size();i++)if(tabs_[i]==page_){lv_dropdown_set_selected(menu_,i);found=true;}
 if(!found&&!tabs_.empty())page(tabs_[0]);
}
void PanelDashboard::wiring(){
 section("Board");label(state_["hardware"]["boardProfile"]|"VIEWE");
 auto profiles=state_["settings"]["ui"]["peripheralProfiles"].as<JsonObjectConst>();
 for(JsonPairConst pair:profiles){if(!pair.value().is<JsonArrayConst>())continue;bool any=false;for(JsonVariantConst p:pair.value().as<JsonArrayConst>())if(p!="none"){if(!any){section(pair.key().c_str());any=true;}label(p.as<String>());}}
 section("Pin assignments");for(JsonPairConst slot:state_["settings"]["ui"]["peripheralHelperBindings"].as<JsonObjectConst>()){
  String text;for(JsonPairConst pin:slot.value().as<JsonObjectConst>()){String key=pin.key().c_str();if(key.startsWith("LED_")||pin.value().is<JsonObjectConst>()||pin.value().is<JsonArrayConst>())continue;text+=key+": GPIO"+valueText(pin.value())+"\n";}
  if(text.length())label(String(slot.key().c_str())+"\n"+text);
 }
 arrays(false);
}
void PanelDashboard::arrays(bool controls){
 for(JsonPairConst slot:state_["settings"]["ui"]["peripheralHelperBindings"].as<JsonObjectConst>()){
  JsonObjectConst values=slot.value().as<JsonObjectConst>();if(values["LED_COUNT"].isNull())continue;
  String base="ui/peripheralHelperBindings/"+String(slot.key().c_str())+"/";int count=values["LED_ARRAYS"]|1;
  if(controls)field("Sync all arrays",base+"LED_SYNC","switch");
  String effects;for(JsonVariantConst effect:state_["ledCapabilities"]["effects"].as<JsonArrayConst>()){if(effects.length())effects+='\n';effects+=effect.as<String>();}if(effects.isEmpty())effects="solid";
  for(int i=0;i<count&&i<8;i++){
   String prefix=i?base+"LED_ARRAY_ITEMS/"+String(i-1)+"/":base;section(String(slot.key().c_str())+" / Array "+String(i+1));
   if(!controls){JsonVariantConst item=i?values["LED_ARRAY_ITEMS"][i-1]:slot.value();label(String(item["LED_LAYOUT"]|"strip")+" / "+String(item["LED_COUNT"]|values["LED_COUNT"]|1)+" pixels");continue;}
   field("Effect",prefix+"LED_DEFAULT_EFFECT","select",effects);
   field("Brightness (%)",prefix+"LED_DEFAULT_BRIGHTNESS","slider","",0,100);
   field("Speed (%)",prefix+"LED_DEFAULT_EFFECTSPEED","slider","",0,100);
   for(const char* color:{"RED","GREEN","BLUE"})field(color,prefix+"LED_DEFAULT_"+color,"slider","",0,255);
  }
 }
}
void PanelDashboard::page(const String& key){
 page_=key;refreshNow_=true;lv_keyboard_set_textarea(keyboard_,nullptr);lv_obj_add_flag(keyboard_,LV_OBJ_FLAG_HIDDEN);lv_obj_set_height(screen_,lv_disp_get_ver_res(display_));fields_.clear();buttons_.clear();labels_.clear();lv_obj_clean(body_);
 for(size_t i=0;i<tabs_.size();i++)if(tabs_[i]==key)lv_dropdown_set_selected(menu_,i);
 if(key=="gpio")wiring();
 if(key=="logics"){
  labels_["logics"]=label("");for(const char* mode:{"playing","paused","stopped"})button(mode,String("logic:")+mode);
  for(JsonObjectConst g:state_["logics"]["groups"].as<JsonArrayConst>()){section(g["name"]|g["id"]|"Group");for(const char* mode:{"playing","paused","stopped"})button(mode,"group:"+String(g["id"]|"")+":"+mode);}
  section("Nodes");for(JsonObjectConst n:state_["logics"]["graph"]["nodes"].as<JsonArrayConst>())label(String(n["label"]|n["type"]|"Node")+"\n"+(n["id"]|""));
 }
 if(key=="wled"){labels_["ledLive"]=label("");arrays(true);}
 if(key=="motor"){labels_["motor"]=label("");for(int i=0;i<2;i++){section("Channel "+String(i+1));button("Forward (1 second)","motor:"+String(i)+":forward");button("Reverse (1 second)","motor:"+String(i)+":reverse");}}
 if(key=="playback"){labels_["playback"]=label("");field("Volume (%)","@volume","slider","",0,100);field("Stream / file URL","@url");button("Play","play");button("Stop","stop");}
 if(key=="effects")for(const char* name:{"startup","alarm","notification","ambientSound","lowBattery","shutDown","updateAvailable","updateSuccess"}){section(name);field("File","effects/"+String(name)+"File");String volume=String(name)=="ambientSound"?"ambient":name;field("Volume (%)","effects/"+volume+"VolumePercent","slider","",0,100);}
 if(key=="wifi"||key=="mqtt")labels_["network"]=label("");
 if(key=="mqtt"){button("Connect","mqtt:connect");button("Disconnect","mqtt:disconnect");button("Rediscover devices","mqtt:rediscover");}
 if(key=="battery")labels_["battery"]=label("");
 if(key=="device"){labels_["device"]=label("");button("Restart device","confirm:reboot");}
 if(key=="oled")label(String(kPanelWidth)+" x "+String(kPanelHeight)+" / CHSC6540");
 if(key=="hardware")labels_["hardware"]=label("");
 if(key.startsWith("storage-")){labels_["storage"]=label("");button("Refresh files","refresh");for(JsonObjectConst f:state_["files"].as<JsonArrayConst>())label(String(f["directory"]|false?LV_SYMBOL_DIRECTORY:LV_SYMBOL_FILE)+" "+(f["name"]|"")+"\n"+String(f["size"]|0)+" bytes");}
 if(key=="firmware"){labels_["firmware"]=label("");button("Check for updates","otaCheck");button("Install selected update","confirm:otaInstall");}
 if(key=="logs")labels_["logs"]=label("");
 if(key=="plots")labels_["plots"]=label("");
 if(key=="security"){
  labels_["security"]=label("");field("PIN","@pin","secret");button("Unlock","security:unlock");
  if(!(state_["security"]["locked"]|false)){if(state_["security"]["enabled"]|false){button("Lock now","security:lock");button("Disable PIN (current PIN)","security:disable");}else{field("New four-digit PIN","@newPin","secret");field("Confirm PIN","@confirmPin","secret");button("Set PIN","security:set");}}
 }
 if(key=="info")labels_["info"]=label("");
 settingsFields();if(!fields_.empty()&&key!="security")button("Save changes","save");update();
}
void PanelDashboard::queue(const String& action,JsonVariantConst args){if(commands_.size()>=8){lv_label_set_text(notice_,"Please wait for pending changes");return;}JsonDocument q;q["action"]=action;q["args"].set(args);String text;serializeJson(q,text);commands_.push_back(text);}
void PanelDashboard::submit(int index){
 JsonDocument patch;auto changes=patch["changes"].to<JsonArray>();
 for(size_t i=0;i<fields_.size();i++){auto& f=fields_[i];if(!f.dirty||(index>=0&&i!=size_t(index))||f.path.startsWith("@"))continue;String text=fieldText(f);if(f.secret&&text.isEmpty())continue;
  auto change=changes.add<JsonObject>();change["path"]=f.path;
  if(f.kind=="switch")change["value"]=text=="true";
  else if(f.kind=="number"||f.kind=="slider"||f.path=="oled/rotation"){
   char* end=nullptr;double value=strtod(text.c_str(),&end);if(text.isEmpty()||*end||!std::isfinite(value)||(f.kind!="select"&&(value<f.minimum||value>f.maximum))){lv_label_set_text(notice_,"Enter a number within the allowed range");noticeUntil_=millis()+5000;return;}
   if(value==std::floor(value))change["value"]=int(value);else change["value"]=value;
  }else change["value"]=text;
 }
 if(changes.size()&&commands_.size()<8){for(JsonObjectConst change:changes)for(auto& f:fields_)if(f.path==change["path"].as<String>())f.submitted=valueText(change["value"]);queue("patch",patch);}
}
void PanelDashboard::event(lv_event_t* e){
 auto* s=static_cast<PanelDashboard*>(lv_event_get_user_data(e));if(s->updating_)return;auto* object=lv_event_get_target(e);auto code=lv_event_get_code(e);
 for(size_t i=0;i<s->fields_.size();i++){auto& f=s->fields_[i];if(f.object!=object)continue;
  if(code==LV_EVENT_CLICKED&&(f.kind=="text"||f.kind=="secret"||f.kind=="number")){lv_keyboard_set_mode(s->keyboard_,f.kind=="number"?LV_KEYBOARD_MODE_NUMBER:LV_KEYBOARD_MODE_TEXT_LOWER);lv_keyboard_set_textarea(s->keyboard_,object);lv_obj_clear_flag(s->keyboard_,LV_OBJ_FLAG_HIDDEN);lv_obj_set_height(s->screen_,lv_disp_get_ver_res(s->display_)*52/100);lv_obj_scroll_to_view_recursive(object,LV_ANIM_ON);}
  if(code==LV_EVENT_VALUE_CHANGED){f.dirty=true;s->drafts_[f.path]=s->fieldText(f);f.submitted="";if(f.kind=="switch"||f.kind=="select")s->submit(i);}
  if(code==LV_EVENT_RELEASED&&f.kind=="slider"){f.dirty=true;if(f.path=="@volume"){JsonDocument a;a["value"]=lv_slider_get_value(object);s->queue("volume",a);f.dirty=false;}else s->submit(i);}return;
 }
 if(code!=LV_EVENT_CLICKED)return;String key;for(auto& pair:s->buttons_)if(pair.second==object){key=pair.first;break;}if(key.isEmpty())return;
 auto text=[s](const char* path){for(auto& f:s->fields_)if(f.path==path)return s->fieldText(f);return String("");};JsonDocument a;
 if(key=="save"){s->submit();return;}if(key=="refresh"){s->page(s->page_);return;}
 if(key.startsWith("confirm:")){s->button("Confirm",key.substring(8));return;}
 if(key.startsWith("logic:")){a["mode"]=key.substring(6);s->queue("logics",a);return;}
 if(key.startsWith("group:")){int split=key.lastIndexOf(':');a["group"]["id"]=key.substring(6,split);a["group"]["mode"]=key.substring(split+1);s->queue("logics",a);return;}
 if(key.startsWith("mqtt:")){a["action"]=key.substring(5);s->queue("mqtt",a);return;}
 if(key.startsWith("security:")){a["action"]=key.substring(9);a["pin"]=text("@pin");a["newPin"]=text("@newPin");a["confirmPin"]=text("@confirmPin");s->queue("security",a);return;}
 if(key.startsWith("motor:")){a["channel"]=key.substring(6,7).toInt();a["forward"]=key.endsWith("forward");a["durationMs"]=1000;s->queue("motor",a);return;}
 if(key=="play")a["url"]=text("@url");s->queue(key,a);
}
void PanelDashboard::statusBar(const AppStateSnapshot& app){
 bool sta=app.network.wifiConnected,ap=app.network.apMode;lv_label_set_text(wifi_,(String(LV_SYMBOL_WIFI)+(sta?(ap?" S+A":" STA"):ap?" AP":" " LV_SYMBOL_CLOSE)).c_str());lv_obj_set_style_text_color(wifi_,lv_color_hex(sta?0x72dc9e:ap?0x60a5fa:0xf87171),0);
 int level=sta?(app.network.wifiRssi>=-55?4:app.network.wifiRssi>=-67?3:app.network.wifiRssi>=-78?2:1):0;
 for(int i=0;i<4;i++)lv_obj_set_style_bg_color(bars_[i],lv_color_hex(i<level?0x72dc9e:0x475569),0);
 lv_label_set_text(mqtt_,app.network.mqttConnected?LV_SYMBOL_SHUFFLE " MQTT":LV_SYMBOL_SHUFFLE " MQTT" LV_SYMBOL_CLOSE);lv_obj_set_style_text_color(mqtt_,lv_color_hex(app.network.mqttConnected?0x72dc9e:0xf87171),0);
 String extra;if(app.playback.state=="playing")extra=LV_SYMBOL_PLAY;if(state_["caps"]["battery"]==true)extra+=" "+String(app.battery.voltage,1)+"V";else if(state_["caps"]["storage-external"]==true)extra+=" SD";lv_label_set_text(extra_,extra.c_str());
 // Keep connection indicators and the clock readable on the 240 px panel.
 if(lv_disp_get_hor_res(display_)<300)lv_obj_add_flag(extra_,LV_OBJ_FLAG_HIDDEN);else lv_obj_clear_flag(extra_,LV_OBJ_FLAG_HIDDEN);
 time_t now=time(nullptr);char clock[6]="--:--";if(now>1577836800){tm local;localtime_r(&now,&local);strftime(clock,sizeof(clock),"%H:%M",&local);}lv_label_set_text(clock_,clock);
}
void PanelDashboard::update(){
 updating_=true;auto set=[this](const char* key,const String& text){auto it=labels_.find(key);if(it!=labels_.end()&&String(lv_label_get_text(it->second))!=text)lv_label_set_text(it->second,text.c_str());};
 auto live=state_["live"];set("network",String(live["wifiConnected"]==true?"Connected":"Offline / AP")+"\n"+(live["ip"]|"")+" / "+String(live["rssi"]|0)+" dBm\nMQTT: "+(live["mqttConnected"]==true?"connected":"disconnected"));
 set("device",String(live["name"]|"ELMA")+"\n"+(live["ip"]|""));set("playback",String(live["title"]|"Idle")+"\n"+(live["playbackState"]|"idle")+" / "+String(live["volume"]|0)+"%");
 set("battery",String(live["voltage"]|0.0f,2)+" V");
 String logic=String("Logics: ")+(state_["logics"]["mode"]|"stopped");for(JsonObjectConst group:state_["logics"]["groups"].as<JsonArrayConst>())logic+="\n"+String(group["name"]|group["id"]|"Group")+": "+(group["mode"]|"playing");logic+="\n"+String(state_["logics"]["live"]["error"]|"");for(JsonPairConst activity:state_["logics"]["live"]["activity"].as<JsonObjectConst>())if(activity.value()["failed"]==true)logic+="\nFailed: "+String(activity.key().c_str());set("logics",logic);
 auto system=state_["system"],hardware=state_["hardware"];set("hardware",String(hardware["chipModel"]|"")+"\nCPU: "+String(hardware["cpuFreqMHz"]|0)+" MHz / "+String(system["cpuLoadPercent"]|0)+"%\nFree heap: "+String(system["freeHeap"]|0)+" B\nLargest block: "+String(system["largestHeapBlockBytes"]|0)+" B\nTemperature: "+valueText(system["chipTemperatureC"])+" C\nPSRAM free: "+valueText(system["psram"]["freeBytes"])+" B");
 set("info",String("ELMA IoT ")+(state_["version"]|"")+"\n"+(hardware["boardProfile"]|"")+"\n"+(live["ip"]|"")+"\nDisplay: "+String(kPanelWidth)+" x "+String(kPanelHeight));
 set("firmware",String("Installed: ")+(state_["version"]|"")+"\n"+(live["otaPhase"]|"idle")+" "+String(live["otaProgress"]|0)+"%\n"+(live["lastError"]|""));
 set("storage",String(state_["storage"]["mounted"]==true?"Mounted":"Unavailable")+"\nFree: "+valueText(state_["storage"]["free"])+" / "+valueText(state_["storage"]["total"])+" bytes");
 set("logs",state_["logText"]|"No log entries");
 String samples;for(JsonObjectConst sample:state_["plots"]["samples"].as<JsonArrayConst>()){samples+=String(sample["plot"]|"")+" / "+(sample["series"]|"")+": "+valueText(sample["value"])+" "+(sample["unit"]|"")+"\n";}set("plots",samples.isEmpty()?String("Waiting for samples"):samples);
 String leds;for(JsonObjectConst led:state_["ledArrays"].as<JsonArrayConst>())leds+="GPIO"+String(led["pin"]|0)+": "+(led["effect"]|"solid")+" / "+String(led["brightness"]|0)+"%\n";set("ledLive",leds);
 set("motor",valueText(state_["motor"]));set("security",String(state_["security"]["locked"]==true?"Locked":"Unlocked")+"\nPIN: "+(state_["security"]["enabled"]==true?"set":"not set")+"\nRetry in: "+String(state_["security"]["retryAfterSeconds"]|0)+" s");
 for(auto& f:fields_){JsonVariantConst incoming=PanelSettings::get(state_["settings"],f.path.c_str());if(f.path=="@volume")incoming=live["volume"];if(f.path=="@url")incoming=state_["settings"]["audio"]["lastPlayback"]["url"];
  String text=valueText(incoming);if(f.secret)continue;if(f.dirty){if(f.submitted.length()&&PanelSettings::acknowledged(f.submitted.c_str(),text.c_str())){f.dirty=false;f.submitted="";drafts_.erase(f.path);}else continue;}
  if(lv_obj_has_state(f.object,LV_STATE_PRESSED)||lv_keyboard_get_textarea(keyboard_)==f.object)continue;
  if(f.kind=="switch"){if(incoming==true)lv_obj_add_state(f.object,LV_STATE_CHECKED);else lv_obj_clear_state(f.object,LV_STATE_CHECKED);}
  else if(f.kind=="slider")lv_slider_set_value(f.object,incoming|0,LV_ANIM_OFF);
  else if(f.kind=="select"){if(lv_dropdown_is_open(f.object))continue;for(int i=0;i<lv_dropdown_get_option_cnt(f.object);i++)if(itemAt(f.options,i)==text){lv_dropdown_set_selected(f.object,i);break;}}
  else if(String(lv_textarea_get_text(f.object))!=text)lv_textarea_set_text(f.object,text.c_str());
 }
 updating_=false;
}
void PanelDashboard::loop(const AppStateSnapshot& app,Snapshot snapshot,Command command,const String& overlay){
 auto now=millis();lv_tick_inc(now-tick_);tick_=now;
 if(refreshNow_||now-refresh_>=1000){bool rebuild=refreshNow_;refreshNow_=false;refresh_=now;state_.clear();if(snapshot)snapshot(page_,state_.to<JsonObject>());
  auto live=state_["live"].to<JsonObject>();live["name"]=app.device.friendlyName;live["ip"]=app.network.ip;live["wifiConnected"]=app.network.wifiConnected;live["mqttConnected"]=app.network.mqttConnected;live["rssi"]=app.network.wifiRssi;live["voltage"]=app.battery.voltage;live["title"]=app.playback.title;live["playbackState"]=app.playback.state;live["volume"]=app.playback.volumePercent;live["lastError"]=app.system.lastError;live["otaPhase"]=app.ota.phase;live["otaProgress"]=app.ota.progressPercent;
  String structure=page_;serializeJson(state_["settings"]["ui"]["peripheralProfiles"],structure);
  for(JsonPairConst slot:state_["settings"]["ui"]["peripheralHelperBindings"].as<JsonObjectConst>())structure+=String(slot.key().c_str())+":"+String(slot.value()["LED_ARRAYS"]|1);
  bool editing=false;for(const auto& f:fields_)if(f.dirty||lv_keyboard_get_textarea(keyboard_)==f.object||lv_obj_has_state(f.object,LV_STATE_PRESSED)||(f.kind=="select"&&lv_dropdown_is_open(f.object)))editing=true;
  if(structure!=structure_&&!editing){structure_=structure;rebuild=true;}
  statusBar(app);syncMenu();if(rebuild){page(page_);refreshNow_=false;}update();
  if(overlay.length())lv_label_set_text(notice_,overlay.c_str());else if(int32_t(now-noticeUntil_)>=0)lv_label_set_text(notice_,"");
 }
 lv_timer_handler();
 if(!commands_.empty()){String encoded=commands_.front();commands_.pop_front();JsonDocument request;deserializeJson(request,encoded);String error;bool ok=command&&command(request["action"].as<String>(),request["args"],error);lv_label_set_text(notice_,ok?"Applied":error.c_str());noticeUntil_=now+4000;
  if(ok&&request["action"]=="patch")for(JsonObjectConst change:request["args"]["changes"].as<JsonArrayConst>()){String path=change["path"]|"";auto it=drafts_.find(path);if(it!=drafts_.end()&&it->second==valueText(change["value"]))drafts_.erase(it);}
  if(ok)for(auto& f:fields_)if(f.secret){updating_=true;lv_textarea_set_text(f.object,"");updating_=false;f.dirty=false;drafts_.erase(f.path);}
  if(request["action"]=="security"){page("security");refreshNow_=true;}
 }
}
#endif
