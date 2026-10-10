#pragma once
#if APP_ROTARY_HMI
#include "panel_dashboard.h"
#include "interface_security.h"
#include <array>

#if APP_ROTARY_HMI_SMOKE
// The hardware smoke image has no settings, security, metrics or OTA services.
// Keep its existing diagnostic menu without pulling in the full application.
class RoundSystemDashboard {
public:
    void begin(lv_obj_t*,PanelDisplay&,const String&){}
    void loop(const AppStateSnapshot&,const PanelDashboard::Snapshot&,const PanelDashboard::Command&){}
    bool enabled()const{return false;}
    void rotate(int){} void press(){} void back(){} void end(){}
    void useCustomMenu(){} void inputActivity(){}
};
#else
class RoundSystemDashboard {
public:
    void begin(lv_obj_t* screen,PanelDisplay& panel,const String& configuration);
    void loop(const AppStateSnapshot&,const PanelDashboard::Snapshot&,const PanelDashboard::Command&);
    bool enabled()const{return enabled_;}
    void rotate(int delta);void press();void back();void end();
    void useCustomMenu();
    void inputActivity(){if(screen_)activity();}
private:
    struct Row {String label,value,id;uint32_t color=0x39baff;};
    enum View {Clock,Menu,Page,Wiring,Confirm,Pin,Locked};
    lv_obj_t* screen_=nullptr;PanelDisplay* panel_=nullptr;
    lv_obj_t *title_=nullptr,*primary_=nullptr,*subtitle_=nullptr,*footer_=nullptr,*gauge_=nullptr;
    std::array<lv_obj_t*,12> badges_{};std::array<lv_obj_t*,3> rowWidgets_{};
    std::array<lv_obj_t*,3> rowLabels_{},rowValues_{};
    std::array<lv_obj_t*,24> clockStatus_{};
    std::array<lv_obj_t*,8> clockDigits_{};
    uint32_t clockFrame_=0;
    String clockStatusText_;
    int clockPalette_=-1;
    std::array<Row,32> rows_{};int rowCount_=0,selected_=0,section_=0,metric_=0;
    int paintedSelection_=-1;
    std::array<std::array<lv_point_t,3>,18> wires_{};
    uint32_t brightnessChanged_=0;bool brightnessPending_=false;
    String wiringSignature_;
    View view_=Clock;bool enabled_=false,customBackground_=false,dirty_=true,editBrightness_=false,confirmation_=false;
    uint32_t refresh_=0,lastInput_=0,timeout_=60,clockSecond_=UINT32_MAX,lastRotation_=0;
    int brightness_=100,pinDigit_=0,pinStage_=0;String pinAction_,pinDraft_,firstPin_,oldTicket_,notice_;
    InterfaceSecurity& security_=InterfaceSecurity::device();JsonDocument cache_{panelJsonAllocator()};AppStateSnapshot live_;
    const PanelDashboard::Command* command_=nullptr;
    char logs_[4097]{};uint16_t logOffsets_[50]{};unsigned logCount_=0;uint64_t logSequence_=UINT64_MAX;
    bool followLogs_=true;int logSelected_=0;
    void draw();void update();void refreshRows();void paintRows();void selectSection(int);
    void activity();void message(const String&);bool execute(const String&,JsonVariantConst);
    void patch(const char*,JsonVariantConst);void pinStart(const String&,int stage);void pinSubmit();
    void row(const String&,const String&,const String& id="",uint32_t color=0x39baff);
    void drawWiring();void updateGauge();void updateClockStatus();void updateClockRing();void securityStatus(JsonObject);
    static void clicked(lv_event_t*);
};
#endif
#endif
