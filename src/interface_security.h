#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Device-wide state. Never serialized with ordinary settings/backups.
class InterfaceSecurity {
public:
    void begin();
    void tick();
    bool locked();
    void status(JsonObject);
    int command(JsonVariantConst,JsonObject);
    void externalLock(bool);
private:
    SemaphoreHandle_t mutex_=nullptr;
    Preferences preferences_;
    bool ready_=false,enabled_=false,locked_=true;
    String salt_,hash_,ticket_;
    uint32_t timeout_=300,failures_=0,cooldown_=0;
    uint64_t activity_=0,waitUntil_=0,ticketUntil_=0;
    static uint64_t now();
    static String randomHex();
    static String digest(const String&,const String&);
    static bool validPin(const String&);
    static bool equal(const String&,const String&);
    bool save();
    void expire();
    bool verify(const String&);
    void writeStatus(JsonObject);
};
