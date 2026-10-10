#include "interface_security.h"
#include <esp_system.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#if APP_ROTARY_HMI
#define mbedtls_sha256_ret mbedtls_sha256
#endif

namespace {
struct Guard { SemaphoreHandle_t mutex; explicit Guard(SemaphoreHandle_t m):mutex(m){if(m)xSemaphoreTake(m,portMAX_DELAY);} ~Guard(){if(mutex)xSemaphoreGive(mutex);} };
}
uint64_t InterfaceSecurity::now(){return esp_timer_get_time()/1000ULL;}
String InterfaceSecurity::randomHex(){uint8_t bytes[16];esp_fill_random(bytes,sizeof(bytes));String result;for(auto byte:bytes){char hex[3];snprintf(hex,sizeof(hex),"%02x",byte);result+=hex;}return result;}
String InterfaceSecurity::digest(const String& salt,const String& pin){String input=salt+":"+pin;uint8_t hash[32];mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(input.c_str()),input.length(),hash,0);String result;for(auto byte:hash){char hex[3];snprintf(hex,sizeof(hex),"%02x",byte);result+=hex;}return result;}
bool InterfaceSecurity::validPin(const String& pin){if(pin.length()!=4)return false;for(unsigned i=0;i<4;i++)if(pin[i]<'0'||pin[i]>'9')return false;return true;}
bool InterfaceSecurity::equal(const String& a,const String& b){if(a.length()!=b.length())return false;uint8_t difference=0;for(unsigned i=0;i<a.length();i++)difference|=a[i]^b[i];return difference==0;}
void InterfaceSecurity::begin(const char* storageNamespace){
    if(mutex_)return;
    mutex_=xSemaphoreCreateMutex();
    if(!mutex_)return;
    Guard guard(mutex_);
    if(!preferences_.begin(storageNamespace,false))return;
    const String stored=preferences_.getString("state","");
    if(stored.length()){
        JsonDocument record;if(deserializeJson(record,stored))return;
        enabled_=record["enabled"]|false;locked_=record["locked"]|true;
        salt_=record["salt"]|"";hash_=record["hash"]|"";
        timeout_=constrain(record["timeout"]|300U,15U,86400U);
        failures_=record["failures"]|0U;cooldown_=min(record["cooldown"]|0U,7200U);
        if(enabled_&&(salt_.length()!=32||hash_.length()!=64))return;
    }else locked_=false;
    activity_=now();waitUntil_=activity_+uint64_t(cooldown_)*1000;
    // A reboot restarts the stored penalty; rebooting never bypasses throttling.
    ready_=true;
}
bool InterfaceSecurity::save(){
    JsonDocument record;record["enabled"]=enabled_;record["locked"]=locked_;record["salt"]=salt_;record["hash"]=hash_;record["timeout"]=timeout_;record["failures"]=failures_;record["cooldown"]=cooldown_;
    String encoded;serializeJson(record,encoded);
    if(preferences_.putString("state",encoded)!=encoded.length()){ready_=false;locked_=true;return false;}return true;
}
void InterfaceSecurity::expire(){
    if(!ready_)return;
    if(enabled_&&!locked_&&now()-activity_>=uint64_t(timeout_)*1000){locked_=true;ticket_="";save();}
    if(cooldown_&&now()>=waitUntil_){cooldown_=0;save();}
    if(ticket_.length()&&now()>=ticketUntil_)ticket_="";
}
void InterfaceSecurity::tick(){if(!mutex_)return;Guard guard(mutex_);expire();}
bool InterfaceSecurity::locked(){if(!mutex_)return true;Guard guard(mutex_);expire();return !ready_||locked_;}
void InterfaceSecurity::writeStatus(JsonObject out){
    out["enabled"]=enabled_;out["locked"]=!ready_||locked_;out["available"]=ready_;out["timeoutSeconds"]=timeout_;
    const auto timestamp=now();out["retryAfterSeconds"]=cooldown_&&waitUntil_>timestamp?uint32_t((waitUntil_-timestamp+999)/1000):0;
    out["attemptsRemaining"]=failures_<5?5-failures_:0;
}
void InterfaceSecurity::status(JsonObject out){if(!mutex_){out["locked"]=true;out["available"]=false;return;}Guard guard(mutex_);expire();writeStatus(out);}
bool InterfaceSecurity::verify(const String& pin){
    if(validPin(pin)&&equal(digest(salt_,pin),hash_)){failures_=0;cooldown_=0;waitUntil_=0;return save();}
    failures_=min<uint32_t>(failures_+1,32);
    // Five wrong attempts are immediate; the sixth starts 1 minute, then 2,
    // 4, 8 ... up to 120 minutes. All callers share this counter.
    if(failures_>=6){cooldown_=min<uint32_t>(uint32_t(60)<<min<uint32_t>(failures_-6,7),7200);waitUntil_=now()+uint64_t(cooldown_)*1000;}
    save();return false;
}
int InterfaceSecurity::command(JsonVariantConst input,JsonObject out){
    if(!mutex_){out["error"]="Security unavailable";return 503;}Guard guard(mutex_);expire();
    auto finish=[&](int code,const char* error=""){writeStatus(out);if(*error)out["error"]=error;else out["ok"]=true;return ready_?code:503;};
    if(!ready_)return finish(503,"Security storage unavailable");
    const String action=input["action"]|"";
    if(action=="activity"){if(!locked_)activity_=now();return finish(200);}
    if(action=="lock"){if(!enabled_)return finish(409,"Set a PIN first");locked_=true;ticket_="";save();return finish(200);}
    if(action=="timeout"){
        if(locked_)return finish(423,"Unlock first");
        int seconds=input["timeoutSeconds"]|0;if(seconds<15||seconds>86400)return finish(400,"Timeout must be 15-86400 seconds");
        timeout_=seconds;activity_=now();save();return finish(200);
    }
    if(action=="set"){
        if(enabled_||locked_)return finish(409,"PIN already configured or interface locked");
        String pin=input["newPin"]|"";if(!validPin(pin)||pin!=String(input["confirmPin"]|""))return finish(400,"Enter the same four digits twice");
        int seconds=input["timeoutSeconds"]|300;if(seconds<15||seconds>86400)return finish(400,"Timeout must be 15-86400 seconds");
        salt_=randomHex();hash_=digest(salt_,pin);enabled_=true;locked_=false;timeout_=seconds;activity_=now();failures_=cooldown_=0;ticket_="";save();return finish(200);
    }
    if(!enabled_)return finish(409,"PIN lock is not configured");
    if(cooldown_)return finish(429,"Too many attempts. Wait before trying again.");
    if(action=="change"){
        if(ticket_.isEmpty()||!equal(ticket_,String(input["ticket"]|"")))return finish(403,"Verify the old PIN again");
        String pin=input["newPin"]|"";if(!validPin(pin)||pin!=String(input["confirmPin"]|""))return finish(400,"Enter the same four digits twice");
        salt_=randomHex();hash_=digest(salt_,pin);locked_=false;ticket_="";activity_=now();save();return finish(200);
    }
    if(action!="unlock"&&action!="verify"&&action!="disable")return finish(400,"Unknown security action");
    if(failures_==5){failures_=6;cooldown_=60;waitUntil_=now()+60000;save();return finish(429,"Too many attempts. Wait before trying again.");}
    if(!verify(input["pin"]|""))return finish(cooldown_?429:403,"Incorrect PIN");
    if(action=="verify"){locked_=true;ticket_=randomHex();ticketUntil_=now()+120000;out["ticket"]=ticket_;}
    else {locked_=false;activity_=now();ticket_="";if(action=="disable"){enabled_=false;salt_="";hash_="";}}
    save();return finish(200);
}
void InterfaceSecurity::externalLock(bool value){if(!mutex_)return;Guard guard(mutex_);if(!ready_||(!value&&enabled_))return;locked_=value;ticket_="";save();}
