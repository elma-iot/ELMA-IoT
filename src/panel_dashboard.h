#pragma once
#if APP_HAS_ONBOARD_PANEL
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <functional>
#include <map>
#include <vector>
#include <deque>
#include "app_state.h"
#include "storage_memory.h"
#include "panel_memory.h"
class PanelDisplay;
class PanelDashboard {
public:
 using Snapshot=std::function<void(const String&,JsonObject)>;
 using Command=std::function<bool(const String&,JsonVariantConst,String&)>;
 explicit PanelDashboard(PanelDisplay& panel):panel_(panel),state_(panelJsonAllocator()){}
 ~PanelDashboard();bool begin(uint8_t rotation);
 void loop(const AppStateSnapshot&,const Snapshot&,const Command&,const String&);
 bool touched(){bool v=touched_;touched_=false;return v;}
private:
 struct Field {String path,kind,options;lv_obj_t* object=nullptr;bool dirty=false,secret=false;String submitted;double minimum=-1e9,maximum=1e9;};
 PanelDisplay& panel_;lv_disp_t* display_=nullptr;lv_indev_t* input_=nullptr;
 lv_disp_draw_buf_t drawBuffer_{};lv_disp_drv_t displayDriver_{};lv_indev_drv_t inputDriver_{};lv_color_t* pixels_=nullptr;
 lv_obj_t *screen_=nullptr,*body_=nullptr,*notice_=nullptr,*keyboard_=nullptr,*menu_=nullptr;
 lv_obj_t *wifi_=nullptr,*mqtt_=nullptr,*extra_=nullptr,*clock_=nullptr,*bars_[4]{};
 lv_obj_t *speaker_=nullptr,*volumeOverlay_=nullptr,*volumeSlider_=nullptr,*volumeLabel_=nullptr;
 std::map<String,lv_obj_t*> labels_,buttons_,meters_;std::map<String,String> drafts_;std::vector<Field> fields_;std::vector<String> tabs_;
 JsonDocument state_;std::deque<String> commands_;String page_="gpio",menuSignature_,structure_,countryOptions_="All countries";
 unsigned long tick_=0,refresh_=0,noticeUntil_=0;bool touched_=false,frameReported_=false,updating_=false,refreshNow_=true;
 bool radioRequested_=false,keyboardBindingsDirty_=true,snapshotReady_=false,bodySuspended_=false,metricsLogged_=false;
 unsigned formPage_=0,wifiResultOffset_=0;
 void suspendBody();void centerSpinner();
 lv_obj_t *wifiOverlay_=nullptr,*wifiContent_=nullptr,*wifiMessage_=nullptr,*wifiProgress_=nullptr,*wifiSsid_=nullptr,*wifiPassword_=nullptr;
 enum class WifiStep { Closed, Scanning, Networks, Credentials, Connecting, Success };
 WifiStep wifiStep_=WifiStep::Closed;unsigned long wifiStepAt_=0;String wifiTarget_;
 bool mqttDialog_=false;lv_obj_t *mqttUser_=nullptr,*mqttPort_=nullptr;
 void wifiDialog(WifiStep,const String& ssid="",bool mqtt=false);void closeWifiDialog();void connectWifiDialog();void updateWifiDialog();
 static void wifiDialogEvent(lv_event_t*);
 void hideKeyboard();void bindKeyboardDismiss(lv_obj_t*);
 bool sdFormatPromptShown_=false;lv_obj_t* sdFormatPrompt_=nullptr;
 lv_obj_t *sdFormatProgress_=nullptr,*sdFormatBar_=nullptr;
 static void sdFormatEvent(lv_event_t*);
 String lastPlaybackUrl_,resumeUrl_,resumeTitle_,statusSignature_;
 void page(const String&);void syncMenu();void statusBar(const AppStateSnapshot&);void update();
 lv_obj_t* label(const String&);void button(const String&,const String&);
 void field(const String&,const String&,const String& kind="text",const String& options="",double minimum=-1e9,double maximum=1e9);
 void section(const String&);void queue(const String&,JsonVariantConst);void submit(int index=-1);
 void settingsFields();void arrays(bool controls);void wiring();String fieldText(const Field&)const;
 void player();
 static void event(lv_event_t*);static void flush(lv_disp_drv_t*,const lv_area_t*,lv_color_t*);static void touch(lv_indev_drv_t*,lv_indev_data_t*);
};
#endif
