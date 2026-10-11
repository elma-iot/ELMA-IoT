#include "web_access.h"
#include "web_access_policy.h"
#if defined(ESP32) && !defined(APP_DISABLE_WEB_UI)
#include <ESPmDNS.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_random.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <atomic>
#include <memory>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <mbedtls/platform.h>
#include <mbedtls/base64.h>
#include <mbedtls/ssl_ticket.h>
#include <nvs.h>
#include <cstring>

namespace {
unsigned maxSessions();
size_t internalReserve();
size_t contiguousReserve();
constexpr size_t kPacket = 2048;
std::atomic<bool> enabled{false}, paused{false}, listening{false};
std::atomic<unsigned> sessions{0}, rejected{0}, failures{0}, handshakes{0}, lastHandshakeMs{0};
std::atomic<uint16_t> port{443};
std::atomic<bool> handshakeInProgress{false};
TaskHandle_t listenerTask = nullptr;
SemaphoreHandle_t credentialsMutex = nullptr;
String certificatePem, keyPem, certificateFingerprint;
bool credentialsLoaded = false, mdnsActive = false;
String advertisedName;
uint16_t advertisedTlsPort=0;
uint32_t lastTick = 0;
std::atomic<bool> allocatorInstalled{false};
bool psramTls(){return allocatorInstalled.load() && psramFound();}
unsigned maxSessions(){return psramTls()?3u:1u;}
size_t internalReserve(){return psramTls()?49152:98304;}
size_t contiguousReserve(){return psramTls()?16384:32768;}
#if defined(MBEDTLS_SSL_TICKET_C) && defined(MBEDTLS_SSL_SESSION_TICKETS)
mbedtls_ssl_ticket_context ticketContext;
SemaphoreHandle_t ticketMutex=nullptr;
String ticketCertificate;
std::atomic<bool> ticketsReady{false};
int ticketWrite(void*,const mbedtls_ssl_session* session,unsigned char* start,const unsigned char* end,size_t* length,uint32_t* lifetime){
    if(xSemaphoreTake(ticketMutex,pdMS_TO_TICKS(100))!=pdTRUE)return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    int result=mbedtls_ssl_ticket_write(&ticketContext,session,start,end,length,lifetime);xSemaphoreGive(ticketMutex);return result;
}
int ticketParse(void*,mbedtls_ssl_session* session,unsigned char* bytes,size_t length){
    if(xSemaphoreTake(ticketMutex,pdMS_TO_TICKS(100))!=pdTRUE)return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    int result=mbedtls_ssl_ticket_parse(&ticketContext,session,bytes,length);xSemaphoreGive(ticketMutex);return result;
}
#endif

void* tlsCalloc(size_t count,size_t size) {
    if(size && count>SIZE_MAX/size)return nullptr;
    // Only the new listener's tasks use PSRAM. Existing outbound TLS clients
    // retain the SDK's internal allocation policy; either kind uses heap free.
    if(count*size>=1024 && psramFound() && strcmp(pcTaskGetName(nullptr),"https-tunnel")==0){
        void* memory=heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(memory)return memory;
    }
    return heap_caps_calloc(count,size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
}

String derPem(const unsigned char* bytes,size_t length,const char* kind) {
    size_t encodedLength=0;
    mbedtls_base64_encode(nullptr,0,&encodedLength,bytes,length);
    std::unique_ptr<unsigned char[]> encoded(new(std::nothrow) unsigned char[encodedLength+1]);
    if(!encoded || mbedtls_base64_encode(encoded.get(),encodedLength+1,&encodedLength,bytes,length)!=0)return {};
    String output;output.reserve(encodedLength+128);
    output="-----BEGIN ";output+=kind;output+="-----\n";
    for(size_t i=0;i<encodedLength;i+=64){size_t count=std::min(size_t(64),encodedLength-i);output.concat(reinterpret_cast<const char*>(encoded.get()+i),count);output+='\n';}
    output+="-----END ";output+=kind;output+="-----\n";return output;
}
String fingerprint(const unsigned char* bytes,size_t length) {
    unsigned char hash[32];
#if MBEDTLS_VERSION_MAJOR >= 3
    if(mbedtls_sha256(bytes,length,hash,0)!=0)return {};
#else
    if(mbedtls_sha256_ret(bytes,length,hash,0)!=0)return {};
#endif
    char hex[65];for(size_t i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",hash[i]);return String(hex);
}

void loadCredentials() {
#if defined(MBEDTLS_PLATFORM_MEMORY) && !defined(MBEDTLS_PLATFORM_CALLOC_MACRO) && !defined(MBEDTLS_PLATFORM_FREE_MACRO)
    if(!allocatorInstalled.exchange(true))mbedtls_platform_set_calloc_free(tlsCalloc,heap_caps_free);
#endif
    if (!credentialsMutex) credentialsMutex = xSemaphoreCreateMutex();
#if defined(MBEDTLS_SSL_TICKET_C) && defined(MBEDTLS_SSL_SESSION_TICKETS)
    if(!ticketMutex){ticketMutex=xSemaphoreCreateMutex();mbedtls_ssl_ticket_init(&ticketContext);}
#endif
    if (!credentialsMutex || xSemaphoreTake(credentialsMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    if (!credentialsLoaded) {
        Preferences store;
        if (store.begin("elma_tls", true)) {
            JsonDocument doc;
            String bundle;
            const char* storedKey=store.isKey("der_bundle")?"der_bundle":"bundle";
            if(store.getType(storedKey)==PT_BLOB){
                size_t length=store.getBytesLength(storedKey);
                if(length && length<=16384 && bundle.reserve(length)){
                    auto bytes=std::unique_ptr<char[]>(new(std::nothrow) char[length+1]);
                    if(bytes && store.getBytes(storedKey,bytes.get(),length)==length){
                        bytes[length]=0;
                        auto* raw=reinterpret_cast<unsigned char*>(bytes.get());
                        if(length>=9 && memcmp(raw,"ELT1",4)==0){
                            size_t certLength=raw[4]|(size_t(raw[5])<<8),keyLength=raw[6]|(size_t(raw[7])<<8);
                            if(certLength && keyLength && certLength+keyLength+9==length){
                                certificatePem=derPem(raw+9,certLength,"CERTIFICATE");
                                keyPem=derPem(raw+9+certLength,keyLength,raw[8]==1?"EC PRIVATE KEY":"RSA PRIVATE KEY");
                                certificateFingerprint=fingerprint(raw+9,certLength);
                            }
                        }else bundle=bytes.get();
                    }
                }
            }else bundle=store.getString(storedKey);
            if(bundle.length() && !deserializeJson(doc,bundle)){
                certificatePem=String(doc["certificate"]|"");keyPem=String(doc["key"]|"");certificateFingerprint=String(doc["fingerprint"]|"");
            }
            store.end();
        }
        credentialsLoaded = true;
    }
    xSemaphoreGive(credentialsMutex);
}

int randomBytes(void*, unsigned char* output, size_t length) { esp_fill_random(output, length); return 0; }
bool retry(int error) { return error == MBEDTLS_ERR_SSL_WANT_READ || error == MBEDTLS_ERR_SSL_WANT_WRITE; }
int receive(void* context, unsigned char* output, size_t length) {
    int result = recv(*static_cast<int*>(context), output, length, 0);
    if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return MBEDTLS_ERR_SSL_WANT_READ;
    return result < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : result;
}
int transmit(void* context, const unsigned char* input, size_t length) {
    int result = send(*static_cast<int*>(context), input, length, 0);
    if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return result < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : result;
}
int parseKey(mbedtls_pk_context* key, const String& pem) {
#if MBEDTLS_VERSION_MAJOR >= 3
    return mbedtls_pk_parse_key(key, reinterpret_cast<const unsigned char*>(pem.c_str()), pem.length()+1, nullptr, 0, randomBytes, nullptr);
#else
    return mbedtls_pk_parse_key(key, reinterpret_cast<const unsigned char*>(pem.c_str()), pem.length()+1, nullptr, 0);
#endif
}
struct Session {
    int client = -1, upstream = -1;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config config;
    mbedtls_x509_crt certificate;
    mbedtls_pk_context key;
    unsigned char toHttp[kPacket], toTls[kPacket];
    Session() { mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&config); mbedtls_x509_crt_init(&certificate); mbedtls_pk_init(&key); }
    ~Session() {
        if (client >= 0) close(client);
        if (upstream >= 0) close(upstream);
        mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&config);
        mbedtls_x509_crt_free(&certificate); mbedtls_pk_free(&key);
    }
};

void serve(void* argument) {
    std::unique_ptr<Session> s(static_cast<Session*>(argument));
    bool success = false;
    bool ownsHandshake = true;
    do {
        String cert, key;
        if (!credentialsMutex || xSemaphoreTake(credentialsMutex, pdMS_TO_TICKS(100)) != pdTRUE) break;
        cert = certificatePem; key = keyPem;
        xSemaphoreGive(credentialsMutex);
        if (mbedtls_x509_crt_parse(&s->certificate, reinterpret_cast<const unsigned char*>(cert.c_str()), cert.length()+1) != 0 || parseKey(&s->key, key) != 0) break;
        // PEM copies are no longer needed once parsed; no key is logged or returned.
        cert = ""; key = "";
        if (mbedtls_ssl_config_defaults(&s->config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) break;
        mbedtls_ssl_conf_rng(&s->config, randomBytes, nullptr);
#if defined(MBEDTLS_SSL_TICKET_C) && defined(MBEDTLS_SSL_SESSION_TICKETS)
        if(ticketMutex && xSemaphoreTake(ticketMutex,pdMS_TO_TICKS(100))==pdTRUE){
            String identity=fingerprint(s->certificate.raw.p,s->certificate.raw.len);
            if(!ticketsReady || ticketCertificate!=identity){
                mbedtls_ssl_ticket_free(&ticketContext);mbedtls_ssl_ticket_init(&ticketContext);
                ticketsReady=mbedtls_ssl_ticket_setup(&ticketContext,randomBytes,nullptr,MBEDTLS_CIPHER_AES_256_GCM,3600)==0;
                ticketCertificate=identity;
            }
            xSemaphoreGive(ticketMutex);
            if(ticketsReady)mbedtls_ssl_conf_session_tickets_cb(&s->config,ticketWrite,ticketParse,nullptr);
        }
#endif
        mbedtls_ssl_conf_authmode(&s->config, MBEDTLS_SSL_VERIFY_NONE); // Server authentication; no client certificate required.
#if MBEDTLS_VERSION_MAJOR < 3
        mbedtls_ssl_conf_min_version(&s->config, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
#else
        mbedtls_ssl_conf_min_tls_version(&s->config, MBEDTLS_SSL_VERSION_TLS1_2);
#endif
        if (mbedtls_ssl_conf_own_cert(&s->config, &s->certificate, &s->key) != 0 || mbedtls_ssl_setup(&s->ssl, &s->config) != 0) break;
        mbedtls_ssl_set_bio(&s->ssl, &s->client, transmit, receive, nullptr);
        fcntl(s->client, F_SETFL, fcntl(s->client, F_GETFL, 0) | O_NONBLOCK);
        int noDelay=1;setsockopt(s->client,IPPROTO_TCP,TCP_NODELAY,&noDelay,sizeof(noDelay));
        const uint32_t started = millis();
        int result;
        while ((result = mbedtls_ssl_handshake(&s->ssl)) != 0 && retry(result) && enabled && millis()-started < 8000) vTaskDelay(1);
        if (result != 0 || !enabled) break;
        ++handshakes; lastHandshakeMs = millis()-started;
        handshakeInProgress=false;
        ownsHandshake=false;
        s->upstream = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s->upstream < 0) break;
        sockaddr_in local{}; local.sin_family = AF_INET; local.sin_port = htons(80); local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        timeval timeout{2,0}; setsockopt(s->upstream, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (connect(s->upstream, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) break;
        fcntl(s->upstream, F_SETFL, fcntl(s->upstream, F_GETFL, 0) | O_NONBLOCK);
        setsockopt(s->upstream,IPPROTO_TCP,TCP_NODELAY,&noDelay,sizeof(noDelay));
        size_t httpLength=0,httpOffset=0,tlsLength=0,tlsOffset=0;
        bool clientEof=false,httpEof=false;
        uint32_t progress = millis();
        // Byte-transparent tunnel: HTTP headers, gzip, OTA bodies and upgraded
        // WebSocket frames all retain their existing handler and authentication.
        while (enabled && millis()-progress < 60000) {
            bool moved=false;
            if (!clientEof && httpLength==httpOffset) {
                result=mbedtls_ssl_read(&s->ssl,s->toHttp,sizeof(s->toHttp));
                if (result>0) {httpLength=result;httpOffset=0;moved=true;}
                else if (!result || result==MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) clientEof=true;
                else if (!retry(result)) break;
            }
            if (httpOffset<httpLength) {
                result=send(s->upstream,s->toHttp+httpOffset,httpLength-httpOffset,0);
                if (result>0) {httpOffset+=result;moved=true;}
                else if (result<0 && errno!=EAGAIN && errno!=EWOULDBLOCK) break;
            }
            if (!httpEof && tlsLength==tlsOffset) {
                result=recv(s->upstream,s->toTls,sizeof(s->toTls),0);
                if (result>0) {tlsLength=result;tlsOffset=0;moved=true;}
                else if (!result) httpEof=true;
                else if (errno!=EAGAIN && errno!=EWOULDBLOCK) break;
            }
            if (tlsOffset<tlsLength) {
                result=mbedtls_ssl_write(&s->ssl,s->toTls+tlsOffset,tlsLength-tlsOffset);
                if (result>0) {tlsOffset+=result;moved=true;}
                else if (!retry(result)) break;
            }
            if (httpEof && tlsLength==tlsOffset) {success=true;mbedtls_ssl_close_notify(&s->ssl);break;}
            if (clientEof && httpLength==httpOffset) break;
            if (moved) progress=millis();
            if(moved)taskYIELD();else vTaskDelay(1);
        }
    } while(false);
    if(ownsHandshake)handshakeInProgress=false;
    if (!success) ++failures;
    s.reset(); --sessions;
    vTaskDelete(nullptr);
}

void listenTask(void*) {
    int listener=-1;
    uint16_t boundPort=0;
    for (;;) {
        if (!enabled || boundPort!=port.load()) {
            if (listener>=0) close(listener);
            listener=-1;listening=false;boundPort=0;
        }
        if (!enabled) {vTaskDelay(pdMS_TO_TICKS(250));continue;}
        if (listener<0) {
            listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            if(listener<0){vTaskDelay(pdMS_TO_TICKS(1000));continue;}
            int reuse=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_ANY);address.sin_port=htons(port.load());
            if(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0||listen(listener,6)!=0){close(listener);listener=-1;vTaskDelay(pdMS_TO_TICKS(1000));continue;}
            fcntl(listener,F_SETFL,fcntl(listener,F_GETFL,0)|O_NONBLOCK);
            boundPort=port;listening=true;
        }
        // Leave queued browser connections in the TCP backlog while resources
        // are busy, rather than allocating unbounded handshake tasks.
        // Chunked HTTPS OTA reconnects between requests. Keep accepting under
        // the same memory/handshake bounds while OTA runs, or it would deadlock.
        if(handshakeInProgress || sessions>=maxSessions() || heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<internalReserve() || heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<contiguousReserve()){vTaskDelay(pdMS_TO_TICKS(100));continue;}
        int client=accept(listener,nullptr,nullptr);
        if(client<0){vTaskDelay(pdMS_TO_TICKS(20));continue;}
        auto* session=new(std::nothrow) Session;
        if(!session){close(client);++rejected;continue;}
        session->client=client;++sessions;handshakeInProgress=true;
        TaskHandle_t worker=nullptr;
        if(xTaskCreate(serve,"https-tunnel",8192,session,1,&worker)!=pdPASS){delete session;--sessions;++rejected;handshakeInProgress=false;}
    }
}
}

namespace WebAccess {
String hostname(const SettingsBundle& settings) {
    std::string value;
    if(WebAccessPolicy::hostname(settings.webAccess.hostname.c_str(),value))return String(value.c_str());
    if(WebAccessPolicy::hostname(settings.device.deviceName.c_str(),value))return String(value.c_str());
    String mac=WiFi.macAddress();mac.replace(":","");mac.toLowerCase();return String("elma-")+mac;
}
void tick(const SettingsBundle& settings,bool otaBusy) {
    if(millis()-lastTick<1000)return;
    lastTick=millis();
    loadCredentials();
    paused=otaBusy;
    if(credentialsMutex && xSemaphoreTake(credentialsMutex,pdMS_TO_TICKS(100))==pdTRUE){
        enabled=settings.webAccess.httpsEnabled && credentialsLoaded && certificatePem.length() && keyPem.length();
        xSemaphoreGive(credentialsMutex);
    }
    port=settings.webAccess.httpsPort;
    if(enabled && !listenerTask && xTaskCreate(listenTask,"https-listener",4096,nullptr,1,&listenerTask)!=pdPASS)listenerTask=nullptr;
    String name=hostname(settings);
    bool advertise=settings.webAccess.mdnsEnabled && (WiFi.status()==WL_CONNECTED || WiFi.getMode()==WIFI_AP || WiFi.getMode()==WIFI_AP_STA);
    uint16_t tlsPort=enabled?port.load():0;
    if(mdnsActive && (!advertise || name!=advertisedName || tlsPort!=advertisedTlsPort)){MDNS.end();mdnsActive=false;}
    if(advertise && !mdnsActive){mdnsActive=MDNS.begin(name.c_str());if(mdnsActive){advertisedName=name;advertisedTlsPort=tlsPort;MDNS.addService("http","tcp",80);MDNS.addService("elma","tcp",80);if(tlsPort)MDNS.addService("https","tcp",tlsPort);}}
}
void diagnostics(JsonObject output) {
    output["supported"]=true;output["mdnsActive"]=mdnsActive;output["httpsListening"]=listening.load();
    loadCredentials();
    if(credentialsMutex && xSemaphoreTake(credentialsMutex,pdMS_TO_TICKS(100))==pdTRUE){
        output["certificateInstalled"]=certificatePem.length()>0;output["fingerprintSha256"]=certificateFingerprint;
        xSemaphoreGive(credentialsMutex);
    }
    output["httpsPort"]=port.load();output["activeTlsConnections"]=sessions.load();output["maxTlsConnections"]=maxSessions();
    output["handshakes"]=handshakes.load();output["lastHandshakeMs"]=lastHandshakeMs.load();output["failedConnections"]=failures.load();output["rejectedConnections"]=rejected.load();
    output["internalFreeBytes"]=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    output["largestInternalBlockBytes"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    output["handshakeReserveBytes"]=internalReserve();output["otaActive"]=paused.load();output["otaPausesNewHandshakes"]=false;
    output["tlsLargeAllocationsUsePsram"]=allocatorInstalled.load() && psramFound();
#if defined(MBEDTLS_SSL_TICKET_C) && defined(MBEDTLS_SSL_SESSION_TICKETS)
    output["tlsSessionTickets"]=ticketsReady.load();
#endif
    nvs_stats_t stats{};if(nvs_get_stats(nullptr,&stats)==ESP_OK)output["nvsFreeEntries"]=stats.free_entries;
    output["guidance"]="Install your local CA on Windows/Android; use HTTPS with a matching DNS SAN. HTTP .local is not a secure context. HTTP/IP fallback remains available.";
}
bool importCertificate(const String& certificate,const String& privateKey,String& error) {
    if(certificate.length()<64 || certificate.length()>8192 || privateKey.length()<64 || privateKey.length()>4096){error="Provide PEM certificate (max 8 KiB) and unencrypted key (max 4 KiB).";return false;}
    mbedtls_x509_crt crt;mbedtls_x509_crt_init(&crt);mbedtls_pk_context key;mbedtls_pk_init(&key);
    bool valid=mbedtls_x509_crt_parse(&crt,reinterpret_cast<const unsigned char*>(certificate.c_str()),certificate.length()+1)==0 && parseKey(&key,privateKey)==0;
#if MBEDTLS_VERSION_MAJOR >= 3
    if(valid)valid=mbedtls_pk_check_pair(&crt.pk,&key,randomBytes,nullptr)==0;
#else
    if(valid)valid=mbedtls_pk_check_pair(&crt.pk,&key)==0;
#endif
    String certificateHash;
    std::unique_ptr<unsigned char[]> encodedKey(new(std::nothrow) unsigned char[4096]);
    std::unique_ptr<unsigned char[]> bundle;
    size_t bundleLength=0;
    if(valid && encodedKey){
        int keyLength=mbedtls_pk_write_key_der(&key,encodedKey.get(),4096);
        if(keyLength>0 && crt.raw.len<=8192){
            bundleLength=9+crt.raw.len+keyLength;bundle.reset(new(std::nothrow) unsigned char[bundleLength]);
            if(bundle){
                memcpy(bundle.get(),"ELT1",4);bundle[4]=crt.raw.len&255;bundle[5]=crt.raw.len>>8;
                bundle[6]=keyLength&255;bundle[7]=keyLength>>8;bundle[8]=mbedtls_pk_get_type(&key)==MBEDTLS_PK_RSA?0:1;
                memcpy(bundle.get()+9,crt.raw.p,crt.raw.len);memcpy(bundle.get()+9+crt.raw.len,encodedKey.get()+4096-keyLength,keyLength);
                certificateHash=fingerprint(crt.raw.p,crt.raw.len);
            }
        }
    }
    mbedtls_x509_crt_free(&crt);mbedtls_pk_free(&key);
    if(!valid){error="Invalid PEM certificate/key or the private key does not match.";return false;}
    if(!bundle || !certificateHash.length()){error="Cannot encode certificate/key within available memory.";return false;}
    encodedKey.reset();
    loadCredentials();
    if(!credentialsMutex || xSemaphoreTake(credentialsMutex,pdMS_TO_TICKS(100))!=pdTRUE){error="Certificate store busy.";return false;}
    // Compact DER fits the small shared NVS partition; one blob commits the
    // pair atomically. Legacy PEM JSON remains readable until replacement.
    Preferences store;bool saved=false;
    if(store.begin("elma_tls",false)) {
        saved=store.putBytes("der_bundle",bundle.get(),bundleLength)==bundleLength;
        if(saved && store.isKey("bundle"))store.remove("bundle");
        store.end();
    }
    if(saved){certificatePem=certificate;keyPem=privateKey;certificateFingerprint=certificateHash;credentialsLoaded=true;}
    xSemaphoreGive(credentialsMutex);if(!saved)error="Cannot persist certificate to NVS.";
    return saved;
}
}
#else
namespace WebAccess {
String hostname(const SettingsBundle& settings){return settings.device.deviceName;}
void tick(const SettingsBundle&,bool){}
void diagnostics(JsonObject output){output["supported"]=false;output["guidance"]="HTTPS requires a supported ESP32 build with the web interface enabled.";}
bool importCertificate(const String&,const String&,String& error){error="HTTPS unavailable in this build.";return false;}
}
#endif
