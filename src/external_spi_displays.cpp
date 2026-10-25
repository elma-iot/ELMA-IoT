#include "external_spi_displays.h"
#if !APP_DISABLE_DISPLAY
#include <Adafruit_GC9A01A.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_ST7796S.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_SSD1331.h>
#include <Adafruit_SSD1351.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <SPI.h>
#include <memory>
#include <vector>
#include <ctime>
#include "spi_display_catalog.h"
#include "update_graphics.h"
#include "display_idle_policy.h"
#include "device_log.h"

namespace ExternalDisplays {
namespace {
int pin(JsonVariantConst value){
    if(value.isNull()||value.is<bool>())return -1;
    if(value.is<int>())return value.as<int>();
    const char* text=value.as<const char*>();if(!text||!*text)return -1;
    char* end=nullptr;long result=strtol(text,&end,10);return end&&!*end?int(result):-1;
}
struct Display {
    const SpiDisplays::Controller* controller=nullptr;
    std::unique_ptr<Adafruit_SPITFT> tft;
    std::unique_ptr<Adafruit_SSD1306> ssd;
    std::unique_ptr<Adafruit_SH1106G> sh;
    std::unique_ptr<UpdateGraphics> canvas;
    Adafruit_GFX* gfx=nullptr;
    int backlight=-1;uint8_t brightness=100,lastBrightness=255;String mode="status",signature;
    uint32_t drawnAt=0,activityAt=0;unsigned lastProgress=101,lastDots=4;
    bool sleeping=false,timeoutEnabled=true;uint16_t timeoutSeconds=0;String observed;
    ~Display(){if(backlight>=0){analogWrite(backlight,0);pinMode(backlight,INPUT);}}
    void level(uint8_t value){
        if(lastBrightness==value)return;lastBrightness=value;
        if(backlight>=0)analogWrite(backlight,value*255/100);
        if(ssd){ssd->ssd1306_command(SSD1306_SETCONTRAST);ssd->ssd1306_command(value*255/100);}
        if(sh)sh->setContrast(value*255/100);
        if(tft && !strcmp(controller->driver,"ssd1351")){uint8_t contrast=value*15/100;tft->sendCommand(0xc7,&contrast,1);}
        if(tft && !strcmp(controller->driver,"ssd1331")){uint8_t contrast=value*255/100;for(uint8_t channel:{0x81,0x82,0x83})tft->sendCommand(channel,&contrast,1);}
    }
    void flush(){if(ssd)ssd->display();if(sh)sh->display();}
    void sleep(bool off){
        if(sleeping==off)return;sleeping=off;
        if(ssd)ssd->ssd1306_command(off?SSD1306_DISPLAYOFF:SSD1306_DISPLAYON);
        if(sh)sh->oled_command(off?SH110X_DISPLAYOFF:SH110X_DISPLAYON);
        if(tft)tft->sendCommand(!strncmp(controller->driver,"ssd",3)?(off?0xae:0xaf):(off?0x28:0x29));
        level(off?0:brightness);if(!off)signature="";
    }
    void text(const String& value,int y,int scale){
        String label=value;const int limit=gfx->width()/(6*scale);
        if(int(label.length())>limit)label=label.substring(0,limit-1)+"~";
        gfx->setTextSize(scale);gfx->setTextColor(controller->monochrome?1:0xffff);
        gfx->setCursor((gfx->width()-int(label.length())*6*scale)/2,y);gfx->print(label);
    }
    void status(const AppStateSnapshot& state,const String& temporary){
        const bool oled=!strncmp(controller->driver,"ssd",3)||!strcmp(controller->driver,"sh1106");
        // Clock ticking is not an external event: it must not defeat OLED
        // burn-in protection. Playback/network/content changes and alerts wake it.
        String content=state.playback.state+"|"+state.playback.title+"|"+state.network.ip+"|"+state.system.lastError+"|"+temporary;
        if(content!=observed){observed=content;activityAt=millis();if(oled||temporary.length()||state.system.lastError.length())sleep(false);}
        if(displayTimeoutDue(millis(),activityAt,timeoutEnabled,timeoutSeconds))sleep(true);
        if(sleeping)return;
        if(millis()-drawnAt<500)return;drawnAt=millis();
        String title,detail,footer;
        if(temporary.length()){title=temporary;detail=state.playback.title;}
        else if(state.system.lastError.length()){title="Alert";detail=state.system.lastError;}
        else if(mode=="clock"){
            time_t now=time(nullptr);char clock[12]="--:--:--",date[20]="Waiting for time";
            if(now>1577836800){tm local;localtime_r(&now,&local);strftime(clock,sizeof(clock),"%H:%M:%S",&local);strftime(date,sizeof(date),"%Y-%m-%d",&local);}
            title=clock;detail=date;
        }else{
            title=state.playback.state=="playing"?state.playback.title:state.device.friendlyName;
            detail=state.playback.state=="playing"?String("Playing"):String("Idle");
        }
        footer=state.network.wifiConnected?state.network.ip:state.network.apMode?state.network.apSsid:String("Wi-Fi connecting");
        String next=title+"|"+detail+"|"+footer;if(signature==next)return;signature=next;
        gfx->fillScreen(0);gfx->setTextWrap(false);
        const int scale=gfx->width()>=200 && gfx->height()>=160?2:1;
        text(title,gfx->height()/2-12*scale,scale);text(detail,gfx->height()/2+3*scale,scale);
        if(gfx->height()>=64)text(footer,gfx->height()*3/4+8,1);
        flush();
    }
    bool prepare(){
        canvas.reset(new(std::nothrow) UpdateGraphics());
        lastProgress=101;lastDots=4;drawnAt=0;sleep(false);level(brightness);
        return canvas&&canvas->begin(gfx->width(),gfx->height(),controller->round);
    }
    void progress(uint8_t value){
        if(!canvas)return;
        if(!canvas->render(value,millis()))return;
        // Expand one row in internal stack RAM; no color framebuffer or DMA
        // dependency on PSRAM is needed while the firmware partition is written.
        alignas(4) uint16_t row[480];const int width=gfx->width(),height=gfx->height();
        if(tft){tft->startWrite();tft->setAddrWindow(0,0,width,height);}
        else gfx->fillScreen(0);
        for(int y=0;y<height;++y){
            canvas->row(row,y);
            if(tft)tft->writePixels(row,width,true);
            else for(int x=0;x<width;++x){const uint16_t c=row[x];if(((c>>11)&31)+((c>>5)&63)+(c&31)>24)gfx->drawPixel(x,y,1);}
        }
        if(tft)tft->endWrite();else flush();
    }
};
std::vector<std::unique_ptr<Display>> displays;
std::unique_ptr<SPIClass> hardware;
String configured,overlay;uint32_t overlayUntil=0;bool updating=false;

std::unique_ptr<Display> create(JsonObjectConst values,SPIClass* bus,const OledSettings& defaults){
    const char* id=values["DISPLAY_CONTROLLER"]|"st7789-240";
    const auto* c=SpiDisplays::controller(id);if(!c){DebugLog.printf("[display] Unknown SPI controller %s\n",id);return nullptr;}
    int sck=pin(values["SCK"]),mosi=pin(values["MOSI"]),cs=pin(values["CS"]),dc=pin(values["DC"]),rst=pin(values["RST"]),bl=pin(values["BL"]);
    for(int value:{sck,mosi,cs,dc})if(!GPIO_IS_VALID_OUTPUT_GPIO(value)){DebugLog.println("[display] SPI display requires valid SCK/MOSI/CS/DC pins");return nullptr;}
    for(int value:{rst,bl})if(value>=0&&!GPIO_IS_VALID_OUTPUT_GPIO(value))return nullptr;
    int used[]={sck,mosi,cs,dc,rst,bl};for(unsigned i=0;i<6;++i)for(unsigned j=i+1;j<6;++j)if(used[i]>=0&&used[i]==used[j])return nullptr;
    auto d=std::unique_ptr<Display>(new(std::nothrow) Display());if(!d)return nullptr;d->controller=c;
    d->mode=values["DISPLAY_INTERFACE"]|(c->round?"clock":"status");
    d->brightness=constrain(values["DISPLAY_BRIGHTNESS"]|int(defaults.brightness),0,100);
    d->timeoutEnabled=values["DISPLAY_TIMEOUT_ENABLED"]|defaults.timeoutEnabled;
    const bool oled=!strncmp(c->driver,"ssd",3)||!strcmp(c->driver,"sh1106");
    d->timeoutSeconds=constrain(values["DISPLAY_TIMEOUT_SECONDS"]|(oled?60:0),0,3600);d->activityAt=millis();
    const int rotation=(values["DISPLAY_ROTATION"]|int(defaults.rotation))/90;
    pinMode(cs,OUTPUT);digitalWrite(cs,HIGH);
    if(bus)bus->begin(sck,-1,mosi,-1);
    if(!strcmp(c->driver,"st7789")){
        auto* p=bus?new(std::nothrow) Adafruit_ST7789(bus,cs,dc,rst):new(std::nothrow) Adafruit_ST7789(cs,dc,mosi,sck,rst);
        if(!p)return nullptr;p->init(c->width,c->height);p->setSPISpeed(20000000);d->tft.reset(p);
    }else if(!strcmp(c->driver,"st7735")){
        auto* p=bus?new(std::nothrow) Adafruit_ST7735(bus,cs,dc,rst):new(std::nothrow) Adafruit_ST7735(cs,dc,mosi,sck,rst);
        // In the pinned driver, REDTAB and 144GREENTAB share 0x01. Value 3
        // deliberately chooses its normal 128x160 red-tab fallback branch.
        if(!p)return nullptr;p->initR(c->variant);p->setSPISpeed(20000000);d->tft.reset(p);
    }else if(!strcmp(c->driver,"st7796s")){
        auto* p=bus?new(std::nothrow) Adafruit_ST7796S(bus,cs,dc,rst):new(std::nothrow) Adafruit_ST7796S(cs,dc,mosi,sck,rst);
        if(!p)return nullptr;p->init(c->width,c->height);p->setSPISpeed(20000000);d->tft.reset(p);
    }else if(!strcmp(c->driver,"ili9341")){
        auto* p=bus?new(std::nothrow) Adafruit_ILI9341(bus,dc,cs,rst):new(std::nothrow) Adafruit_ILI9341(cs,dc,mosi,sck,rst);
        if(!p)return nullptr;p->begin(20000000);d->tft.reset(p);
    }else if(!strcmp(c->driver,"gc9a01")){
        auto* p=bus?new(std::nothrow) Adafruit_GC9A01A(bus,dc,cs,rst):new(std::nothrow) Adafruit_GC9A01A(cs,dc,mosi,sck,rst);
        if(!p)return nullptr;p->begin(20000000);d->tft.reset(p);
    }else if(!strcmp(c->driver,"ssd1351")){
        auto* p=bus?new(std::nothrow) Adafruit_SSD1351(c->width,c->height,bus,cs,dc,rst):new(std::nothrow) Adafruit_SSD1351(c->width,c->height,cs,dc,mosi,sck,rst);
        if(!p)return nullptr;p->begin(8000000);d->tft.reset(p);
    }else if(!strcmp(c->driver,"ssd1331")){
        auto* p=bus?new(std::nothrow) Adafruit_SSD1331(bus,cs,dc,rst):new(std::nothrow) Adafruit_SSD1331(cs,dc,mosi,sck,rst);
        if(!p)return nullptr;p->begin(8000000);d->tft.reset(p);
    }else if(!strcmp(c->driver,"ssd1306")){
        d->ssd.reset(bus?new(std::nothrow) Adafruit_SSD1306(c->width,c->height,bus,dc,rst,cs):new(std::nothrow) Adafruit_SSD1306(c->width,c->height,mosi,sck,dc,rst,cs));
        if(!d->ssd||!d->ssd->begin(SSD1306_SWITCHCAPVCC,0,true,false))return nullptr;d->gfx=d->ssd.get();
    }else if(!strcmp(c->driver,"sh1106")){
        d->sh.reset(bus?new(std::nothrow) Adafruit_SH1106G(c->width,c->height,bus,dc,rst,cs):new(std::nothrow) Adafruit_SH1106G(c->width,c->height,mosi,sck,dc,rst,cs));
        if(!d->sh||!d->sh->begin(0,true))return nullptr;d->gfx=d->sh.get();
    }
    if(d->tft)d->gfx=d->tft.get();if(!d->gfx)return nullptr;
    d->gfx->setRotation(rotation&3);d->gfx->fillScreen(0);d->backlight=bl;
    if(bl>=0)pinMode(bl,OUTPUT);d->level(d->brightness);d->flush();
    DebugLog.printf("[display] External %s %dx%d %s SPI\n",id,d->gfx->width(),d->gfx->height(),bus?"hardware":"software");return d;
}
}
void configure(const SettingsBundle& settings){
    String signature=settings.ui.peripheralProfileSelections+settings.ui.peripheralHelperBindings+String(settings.oled.enabled)+String(settings.oled.brightness)+String(settings.oled.rotation)+String(settings.oled.timeoutEnabled);
    if(updating||configured==signature)return;
    configured=signature;displays.clear();if(hardware){hardware->end();hardware.reset();}
    JsonDocument profiles,bindings;if(deserializeJson(profiles,settings.ui.peripheralProfileSelections)||deserializeJson(bindings,settings.ui.peripheralHelperBindings))return;
    unsigned index=0;
    for(JsonVariantConst item:profiles["displayProfiles"].as<JsonArrayConst>()){
        const unsigned slot=index++;if(item!="spi-tft" || (slot==0&&!settings.oled.enabled))continue;
        JsonObjectConst values=bindings["display:"+String(slot)].as<JsonObjectConst>();if(values["DISPLAY_ENABLED"]==false)continue;
        // Independent software SPI is the safe fallback when another display or
        // SD uses the available controller. It never remaps an active SD bus.
        SPIClass* bus=nullptr;
#if !APP_HAS_ONBOARD_PANEL && SOC_SPI_PERIPH_NUM > 2
        if(displays.empty()){
#if defined(CONFIG_IDF_TARGET_ESP32)
            hardware.reset(new(std::nothrow) SPIClass(VSPI));
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
            hardware.reset(new(std::nothrow) SPIClass(HSPI));
#else
            hardware.reset(new(std::nothrow) SPIClass(FSPI));
#endif
            bus=hardware.get();
        }
#endif
        auto display=create(values,bus,settings.oled);if(display)displays.push_back(std::move(display));
    }
}
void loop(const AppStateSnapshot& state){
    if(updating)return;if(overlayUntil && int32_t(millis()-overlayUntil)>=0){overlay="";overlayUntil=0;}
    static size_t cursor=0;if(!displays.empty()){cursor%=displays.size();displays[cursor++]->status(state,overlay);}
}
bool prepareUpdate(){updating=true;for(auto& display:displays)if(!display->prepare())return false;return true;}
void progress(uint8_t percent){static size_t cursor=0;if(!displays.empty()){cursor%=displays.size();displays[cursor++]->progress(percent);}}
void endUpdate(){for(auto& display:displays){display->canvas.reset();display->signature="";}updating=false;}
void temporaryText(const String& text,uint32_t duration){overlay=text;overlayUntil=millis()+duration;}
bool available(){return !displays.empty();}
void powerOff(){for(auto& display:displays)display->sleep(true);}
}
#else
namespace ExternalDisplays {
void configure(const SettingsBundle&){}void loop(const AppStateSnapshot&){}bool prepareUpdate(){return true;}
void progress(uint8_t){}void endUpdate(){}void temporaryText(const String&,uint32_t){}
bool available(){return false;}void powerOff(){}
}
#endif
