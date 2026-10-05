#pragma once
// Pin definitions: rzeldent/platformio-espressif32-sunton, ESP32-2432S028R
// and ESP32-3248S035C. CST820 uses the same coordinate registers as CST816.
#if APP_SUNTON_PANEL
#include <LovyanGFX.hpp>
#include <Wire.h>
class SuntonPanel : public lgfx::LGFX_Device {
    lgfx::Bus_SPI bus_;
#if APP_SUNTON_PANEL == 3
    lgfx::Panel_ST7796 panel_;
#else
    lgfx::Panel_ILI9341 panel_;
#endif
    lgfx::Light_PWM light_;
    TwoWire touchWire_{1};
    bool touchEnabled_ = false;
    uint8_t touchAddress_ = 0;
#if APP_SUNTON_PANEL == 3
    bool touchPressed_ = false;
    int16_t touchX_ = 0, touchY_ = 0;
#endif
    uint16_t readXpt(uint8_t command) {
        digitalWrite(33, LOW);
        for (int bit=7;bit>=0;--bit) {
            digitalWrite(32,(command>>bit)&1); digitalWrite(25,HIGH);
            delayMicroseconds(1);digitalWrite(25,LOW);
        }
        uint16_t data=0;
        for (int bit=0;bit<16;++bit) {
            digitalWrite(25,HIGH);delayMicroseconds(1);data=(data<<1)|digitalRead(39);digitalWrite(25,LOW);
        }
        digitalWrite(33,HIGH);return (data>>3)&4095;
    }
    bool readReg(uint16_t reg,uint8_t* data,size_t count,bool wide) {
        touchWire_.beginTransmission(touchAddress_);
        if(wide)touchWire_.write(reg>>8);
        touchWire_.write(reg&255);
        if(touchWire_.endTransmission(false)!=0)return false;
        if(touchWire_.requestFrom(touchAddress_,uint8_t(count))!=count)return false;
        for(size_t i=0;i<count;++i)data[i]=touchWire_.read();
        return true;
    }
public:
    SuntonPanel() {
        auto b=bus_.config();b.spi_host=SPI2_HOST;b.spi_mode=0;
        b.freq_write=24000000;b.freq_read=16000000;b.spi_3wire=false;
        b.use_lock=true;b.dma_channel=SPI_DMA_CH_AUTO;
        b.pin_sclk=14;b.pin_mosi=13;b.pin_miso=12;b.pin_dc=2;bus_.config(b);panel_.setBus(&bus_);
        auto p=panel_.config();p.pin_cs=15;p.pin_rst=-1;p.pin_busy=-1;
        p.panel_width=kPanelWidth;p.panel_height=kPanelHeight;
        p.memory_width=kPanelWidth;p.memory_height=kPanelHeight;
        p.offset_rotation=0;p.readable=true;p.invert=false;p.rgb_order=false;p.bus_shared=false;panel_.config(p);
        auto l=light_.config();l.pin_bl=APP_SUNTON_PANEL==3?27:21;
        l.invert=false;l.freq=5000;l.pwm_channel=0;light_.config(l);panel_.setLight(&light_);setPanel(&panel_);
    }
    bool start(bool touch) {
        if(!init())return false;
        setRotation(0);touchEnabled_=touch;
        if(!touch)return true;
#if APP_SUNTON_PANEL == 1
        // Software SPI keeps the separate XPT2046 wiring off the SD VSPI host.
        pinMode(33,OUTPUT);digitalWrite(33,HIGH);pinMode(25,OUTPUT);digitalWrite(25,LOW);
        pinMode(32,OUTPUT);pinMode(39,INPUT);pinMode(36,INPUT);
#else
        pinMode(25,OUTPUT);digitalWrite(25,LOW);
#if APP_SUNTON_PANEL == 3
        pinMode(21,OUTPUT);digitalWrite(21,LOW); // GT911 address 0x5d
#endif
        delay(10);digitalWrite(25,HIGH);delay(60);
#if APP_SUNTON_PANEL == 3
        pinMode(21,INPUT);touchAddress_=0x5d;
#else
        touchAddress_=0x15;
#endif
        touchWire_.begin(33,32,400000);touchWire_.setTimeOut(8);
        touchWire_.beginTransmission(touchAddress_);
        touchEnabled_=touchWire_.endTransmission()==0;
#endif
        return true;
    }
    bool point(int16_t& x,int16_t& y) {
        if(!touchEnabled_)return false;
#if APP_SUNTON_PANEL == 1
        if(digitalRead(36))return false;
        if(readXpt(0xB0)<300)return false;
        auto median=[this](uint8_t cmd){uint16_t a=readXpt(cmd),b=readXpt(cmd),c=readXpt(cmd);return max(min(a,b),min(max(a,b),c));};
        // Portrait calibration from the standard CYD 2.8-inch resistive panel.
        x=constrain(map(median(0xD0),200,3900,kPanelWidth-1,0),0,kPanelWidth-1);
        y=constrain(map(median(0x90),200,3900,0,kPanelHeight-1),0,kPanelHeight-1);return true;
#elif APP_SUNTON_PANEL == 3
        uint8_t status=0,data[8];
        if(!readReg(0x814e,&status,1,true)){touchPressed_=false;return false;}
        // No fresh report is not a release: retain a stationary finger until
        // the controller sends its zero-contact report.
        if(!(status&0x80)){x=touchX_;y=touchY_;return touchPressed_;}
        bool valid=(status&15)>0&&readReg(0x8150,data,8,true);
        touchWire_.beginTransmission(touchAddress_);touchWire_.write(0x81);touchWire_.write(0x4e);touchWire_.write(0);touchWire_.endTransmission();
        if(!valid){touchPressed_=false;return false;}
        x=data[0]|(data[1]<<8);y=data[2]|(data[3]<<8);
        touchX_=x;touchY_=y;touchPressed_=x>=0&&x<kPanelWidth&&y>=0&&y<kPanelHeight;
#else
        uint8_t data[5];if(!readReg(2,data,5,false)||!(data[0]&15))return false;
        x=((data[1]&15)<<8)|data[2];y=((data[3]&15)<<8)|data[4];
#endif
        return x>=0&&x<kPanelWidth&&y>=0&&y<kPanelHeight;
    }
};
#endif
