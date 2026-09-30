#pragma once
#if APP_HAS_ONBOARD_PANEL
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <functional>
#include <map>
#include <deque>
#include "app_state.h"
class PanelDisplay;

// All commands are dispatched after LVGL returns, through the web UI's handlers.
class PanelDashboard {
public:
    using Snapshot = std::function<void(JsonObject)>;
    using Command = std::function<bool(const String&, JsonVariantConst, String&)>;
    explicit PanelDashboard(PanelDisplay& panel): panel_(panel) {}
    ~PanelDashboard();
    bool begin(uint8_t rotation);
    void loop(const AppStateSnapshot&, Snapshot, Command, const String& overlay);
    bool touched() { bool value=touched_; touched_=false; return value; }
private:
    PanelDisplay& panel_;
    lv_disp_t* display_=nullptr;
    lv_indev_t* input_=nullptr;
    lv_disp_draw_buf_t drawBuffer_{};
    lv_disp_drv_t displayDriver_{};
    lv_indev_drv_t inputDriver_{};
    lv_color_t* pixels_=nullptr;
    lv_obj_t *screen_=nullptr,*body_=nullptr,*notice_=nullptr,*keyboard_=nullptr,*header_=nullptr;
    std::map<String,lv_obj_t*> fields_;
    JsonDocument state_;
    std::deque<String> commands_;
    unsigned long tick_=0,refresh_=0,noticeUntil_=0;
    bool touched_=false;
    int page_=0;
    void page(int);
    lv_obj_t* label(const String&);
    void button(const char*,const char*);
    void slider(const char*,const char*,int);
    void textField(const char*,const char*,const String&,bool secret=false);
    void queue(const String&,JsonVariantConst);
    void update();
    static void event(lv_event_t*);
    static void flush(lv_disp_drv_t*,const lv_area_t*,lv_color_t*);
    static void touch(lv_indev_drv_t*,lv_indev_data_t*);
};
#endif
