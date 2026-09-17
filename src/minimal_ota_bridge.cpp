#if defined(APP_MINIMAL_OTA_BRIDGE)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_app_format.h>
#include <esp_ota_ops.h>

#include "version.h"

namespace {
AsyncWebServer server(80);
Preferences preferences;
String uploadSession;
String uploadFilename;
size_t uploadExpected = 0;
size_t uploadOffset = 0;
bool uploadActive = false;
bool uploadFailed = false;
String uploadError;
uint8_t imageHeader[sizeof(esp_image_header_t)] = {};
size_t imageHeaderBytes = 0;
unsigned long restartAt = 0;
unsigned long reconnectAt = 0;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
constexpr const char* kChipFamily = "esp32s3";
constexpr esp_chip_id_t kChipId = ESP_CHIP_ID_ESP32S3;
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
constexpr const char* kChipFamily = "esp32s2";
constexpr esp_chip_id_t kChipId = ESP_CHIP_ID_ESP32S2;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
constexpr const char* kChipFamily = "esp32c6";
constexpr esp_chip_id_t kChipId = ESP_CHIP_ID_ESP32C6;
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
constexpr const char* kChipFamily = "esp32c2";
constexpr esp_chip_id_t kChipId = ESP_CHIP_ID_ESP32C2;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
constexpr const char* kChipFamily = "esp32c3";
constexpr esp_chip_id_t kChipId = ESP_CHIP_ID_ESP32C3;
#else
constexpr const char* kChipFamily = "esp32";
constexpr esp_chip_id_t kChipId = ESP_CHIP_ID_ESP32;
#endif

const char kRecoveryPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ELMA OTA Bridge</title><style>body{font:16px system-ui;background:#f7f5ef;color:#292824;margin:0;padding:24px}main{max-width:680px;margin:auto;background:#fff;padding:24px;border:1px solid #d7d3ca;border-radius:14px}button{background:#ef8b00;color:#fff;border:0;border-radius:9px;padding:12px 18px;font-weight:700}progress{width:100%;height:22px}</style></head><body><main><h1>ELMA Minimal OTA Bridge</h1><p>Saved ELMA configuration is preserved. Choose full firmware for this exact ESP chip.</p><input id="file" type="file" accept=".bin"><button id="go">Upload Full Firmware</button><p><progress id="bar" max="100" value="0"></progress></p><p id="status">Ready.</p><script>
const f=document.querySelector('#file'),b=document.querySelector('#bar'),s=document.querySelector('#status'),g=document.querySelector('#go');
async function api(path,options={}){const r=await fetch('/api/firmware/upload/'+path,{cache:'no-store',...options});const v=await r.json();if(!r.ok)throw Error(v.error||'HTTP '+r.status);return v;}
function post(path,value){return api(path,{method:'POST',headers:{'content-type':'application/json'},body:JSON.stringify(value)});}
g.onclick=async()=>{const x=f.files[0];if(!x)return s.textContent='Choose a .bin file.';
const id=Date.now().toString(16)+Math.random().toString(16).slice(2);g.disabled=true;
try{await post('start',{sessionId:id,filename:x.name,size:x.size});let o=0,n=0;
while(o<x.size){try{const z=await x.slice(o,o+8192).arrayBuffer();const v=await api('chunk?sessionId='+id+'&offset='+o,{method:'PUT',body:z});o=v.upload.offset;n=0;b.value=Math.round(o*100/x.size);s.textContent='Uploading '+b.value+'%';}
catch(e){if(++n>8)throw e;s.textContent='Wi-Fi interrupted; resuming ('+n+'/8)…';await new Promise(r=>setTimeout(r,1500));try{const v=await api('status');if(v.upload.sessionId===id)o=v.upload.offset;}catch{}}}
await post('finish',{sessionId:id});s.textContent='Complete. Device is restarting into full firmware.';
}catch(e){s.textContent='Upload failed: '+e.message;}finally{g.disabled=false;}};
</script></main></body></html>)HTML";

void sendJson(AsyncWebServerRequest* request, int status, const String& body) {
    AsyncWebServerResponse* response = request->beginResponse(status, "application/json", body);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

String jsonError(const String& error) {
    JsonDocument doc;
    doc["error"] = error;
    String body;
    serializeJson(doc, body);
    return body;
}

bool compatibleHeader(const uint8_t* data, size_t length, String& error) {
    if (length < sizeof(esp_image_header_t)) {
        error = "Firmware image header is incomplete.";
        return false;
    }
    esp_image_header_t header;
    memcpy(&header, data, sizeof(header));
    if (header.magic != ESP_IMAGE_HEADER_MAGIC) {
        error = "Uploaded file is not an ESP application image.";
        return false;
    }
    if (header.chip_id != kChipId) {
        error = String("Firmware chip does not match this ") + kChipFamily + " device.";
        return false;
    }
    return true;
}

void abortUpload(const String& error) {
    if (uploadActive) {
        Update.abort();
    }
    uploadActive = false;
    uploadFailed = true;
    uploadError = error;
    uploadSession = "";
    uploadFilename = "";
    uploadExpected = 0;
    uploadOffset = 0;
    imageHeaderBytes = 0;
}

bool startUpload(const String& session, const String& filename, size_t size, String& error) {
    if (uploadActive) {
        if (session == uploadSession && filename == uploadFilename && size == uploadExpected) {
            return true;
        }
        error = "Another firmware upload is active.";
        return false;
    }
    if (session.isEmpty() || !filename.endsWith(".bin") || size == 0) {
        error = "A session, .bin filename and non-zero image size are required.";
        return false;
    }
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr || size > target->size) {
        error = "Firmware does not fit the inactive OTA partition.";
        return false;
    }
    if (!Update.begin(size, U_FLASH)) {
        error = String("Unable to open inactive OTA partition: ") + Update.errorString();
        return false;
    }
    uploadSession = session;
    uploadFilename = filename;
    uploadExpected = size;
    uploadOffset = 0;
    imageHeaderBytes = 0;
    uploadActive = true;
    uploadFailed = false;
    uploadError = "";
    return true;
}

bool writeUpload(const String& session, size_t offset, uint8_t* data, size_t length, String& error) {
    if (!uploadActive || session != uploadSession) {
        error = "Firmware upload session is not active.";
        return false;
    }
    if (offset > uploadOffset) {
        error = String("Upload offset mismatch; resume at byte ") + uploadOffset + ".";
        return false;
    }
    if (offset + length <= uploadOffset) {
        return true;
    }
    const size_t acceptedPrefix = uploadOffset - offset;
    data += acceptedPrefix;
    length -= acceptedPrefix;
    if (uploadOffset + length > uploadExpected) {
        error = "Firmware data exceeds the declared image size.";
        abortUpload(error);
        return false;
    }
    const size_t receivedLength = length;
    if (imageHeaderBytes < sizeof(imageHeader)) {
        const size_t copied = min(length, sizeof(imageHeader) - imageHeaderBytes);
        memcpy(imageHeader + imageHeaderBytes, data, copied);
        imageHeaderBytes += copied;
        data += copied;
        length -= copied;
        if (imageHeaderBytes == sizeof(imageHeader)) {
            if (!compatibleHeader(imageHeader, sizeof(imageHeader), error) ||
                Update.write(imageHeader, sizeof(imageHeader)) != sizeof(imageHeader)) {
                if (error.isEmpty()) error = "Unable to write firmware header.";
                abortUpload(error);
                return false;
            }
        }
    }
    if (length > 0 && Update.write(data, length) != length) {
        error = String("Firmware write failed: ") + Update.errorString();
        abortUpload(error);
        return false;
    }
    uploadOffset += receivedLength;
    return true;
}

String uploadStatusJson() {
    JsonDocument doc;
    doc["ok"] = true;
    JsonObject upload = doc["upload"].to<JsonObject>();
    upload["active"] = uploadActive;
    upload["sessionId"] = uploadSession;
    upload["filename"] = uploadFilename;
    upload["offset"] = uploadOffset;
    upload["total"] = uploadExpected;
    upload["progress"] = uploadExpected ? (uploadOffset * 100U) / uploadExpected : 0;
    upload["message"] = uploadFailed ? uploadError : (uploadActive ? "Uploading to inactive partition" : "Ready");
    String body;
    serializeJson(doc, body);
    return body;
}

void configureRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html; charset=utf-8", kRecoveryPage);
    });
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        JsonDocument doc;
        JsonObject firmware = doc["firmware"].to<JsonObject>();
        firmware["version"] = APP_VERSION;
        firmware["chipFamily"] = kChipFamily;
        firmware["variant"] = "minimal-ota-bridge";
        JsonObject system = doc["system"].to<JsonObject>();
        system["hostname"] = WiFi.getHostname();
        system["minimalOtaBridge"] = true;
        JsonObject network = doc["network"].to<JsonObject>();
        network["wifiConnected"] = WiFi.status() == WL_CONNECTED;
        network["apMode"] = WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA;
        network["ip"] = WiFi.localIP().toString();
        network["ssid"] = WiFi.SSID();
        network["apSsid"] = WiFi.softAPSSID();
        network["rssi"] = WiFi.RSSI();
        String body;
        serializeJson(doc, body);
        sendJson(request, 200, body);
    });
    server.on("/api/firmware/upload/status", HTTP_GET, [](AsyncWebServerRequest* request) {
        sendJson(request, 200, uploadStatusJson());
    });

    auto* startHandler = new AsyncCallbackJsonWebHandler("/api/firmware/upload/start", [](AsyncWebServerRequest* request, JsonVariant& json) {
        String error;
        const String session = String(static_cast<const char*>(json["sessionId"] | ""));
        const String filename = String(static_cast<const char*>(json["filename"] | ""));
        const size_t size = json["size"] | 0U;
        if (!startUpload(session, filename, size, error)) {
            sendJson(request, 409, jsonError(error));
            return;
        }
        sendJson(request, 200, uploadStatusJson());
    });
    startHandler->setMethod(HTTP_POST);
    server.addHandler(startHandler);

    server.on("/api/firmware/upload/chunk", HTTP_PUT,
        [](AsyncWebServerRequest*) {}, nullptr,
        [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            if (!request->hasParam("sessionId") || !request->hasParam("offset") || total == 0 || total > 8192U) {
                if (index + len == total) sendJson(request, 400, jsonError("Invalid upload session, offset, or chunk size."));
                return;
            }
            const String session = request->getParam("sessionId")->value();
            const size_t base = strtoull(request->getParam("offset")->value().c_str(), nullptr, 10);
            String error;
            if (!writeUpload(session, base + index, data, len, error)) {
                if (index + len == total) sendJson(request, 409, jsonError(error));
                return;
            }
            if (index + len == total) sendJson(request, 200, uploadStatusJson());
        });

    auto* finishHandler = new AsyncCallbackJsonWebHandler("/api/firmware/upload/finish", [](AsyncWebServerRequest* request, JsonVariant& json) {
        const String session = String(static_cast<const char*>(json["sessionId"] | ""));
        if (!uploadActive || session != uploadSession || uploadOffset != uploadExpected) {
            sendJson(request, 409, jsonError(String("Upload is incomplete; resume at byte ") + uploadOffset + "."));
            return;
        }
        if (!Update.end(true) || !Update.isFinished()) {
            const String error = String("Firmware finalization failed: ") + Update.errorString();
            abortUpload(error);
            sendJson(request, 409, jsonError(error));
            return;
        }
        uploadActive = false;
        uploadSession = "";
        restartAt = millis() + 1500UL;
        sendJson(request, 200, "{\"ok\":true,\"message\":\"Full firmware installed; restarting.\"}");
    });
    finishHandler->setMethod(HTTP_POST);
    server.addHandler(finishHandler);

    auto* cancelHandler = new AsyncCallbackJsonWebHandler("/api/firmware/upload/cancel", [](AsyncWebServerRequest* request, JsonVariant&) {
        abortUpload("Firmware upload cancelled.");
        sendJson(request, 200, "{\"ok\":true}");
    });
    cancelHandler->setMethod(HTTP_POST);
    server.addHandler(cancelHandler);
}

void connectSavedWifi() {
    preferences.begin("notifier", true);
    const String ssid = preferences.getString("wifi_ssid", "");
    const String password = preferences.getString("wifi_pass", "");
    String hostname = preferences.getString("dev_name", "elma-recovery");
    const bool useStatic = preferences.getBool("wifi_static", false);
    const String staticIp = preferences.getString("wifi_ip", "");
    const String gateway = preferences.getString("wifi_gw", "");
    const String subnet = preferences.getString("wifi_sub", "255.255.255.0");
    const String dns1 = preferences.getString("wifi_dns1", "");
    const String dns2 = preferences.getString("wifi_dns2", "");
    preferences.end();

    WiFi.mode(WIFI_AP_STA);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.setHostname(hostname.c_str());
    if (useStatic && !staticIp.isEmpty()) {
        IPAddress ip, gw, mask, primaryDns, secondaryDns;
        if (ip.fromString(staticIp) && gw.fromString(gateway) && mask.fromString(subnet)) {
            primaryDns.fromString(dns1);
            secondaryDns.fromString(dns2);
            WiFi.config(ip, gw, mask, primaryDns, secondaryDns);
        }
    }
    if (!ssid.isEmpty()) {
        WiFi.begin(ssid.c_str(), password.c_str());
    }
    const uint64_t suffix = ESP.getEfuseMac() & 0xFFFFFFULL;
    char apName[32];
    snprintf(apName, sizeof(apName), "ELMA-Recovery-%06llX", suffix);
    WiFi.softAP(apName, "12345678");
    // The old STA power ceiling is intentionally not carried into recovery.
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
}
}  // namespace

void setup() {
    Serial.begin(115200);
    esp_ota_mark_app_valid_cancel_rollback();
    connectSavedWifi();
    configureRoutes();
    server.begin();
    Serial.printf("[minimal-ota] v%s %s bridge ready; saved NVS configuration preserved\n", APP_VERSION, kChipFamily);
}

void loop() {
    if (restartAt != 0 && static_cast<long>(millis() - restartAt) >= 0) {
        ESP.restart();
    }
    if (WiFi.status() != WL_CONNECTED && static_cast<long>(millis() - reconnectAt) >= 0) {
        reconnectAt = millis() + 15000UL;
        WiFi.reconnect();
    }
    delay(uploadActive ? 1 : 25);
}

#endif  // APP_MINIMAL_OTA_BRIDGE
