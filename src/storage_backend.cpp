#if !APP_DISABLE_SD
#include "device_log.h"
#include "storage_backend.h"

#include <LittleFS.h>
#include <SD.h>
#include <sd_diskio.h>
#include <ff.h>
#if defined(CONFIG_IDF_TARGET_ESP32S3) && APP_HAS_ONBOARD_PANEL || APP_HAS_CAMERA
#include <SD_MMC.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>
#include <diskio.h>
#endif
#include <SPI.h>
#include <memory>
#include "psram_allocator.h"
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void serviceStorageBackends();

namespace {
// Arduino ESP32 2.x sd_diskio caps transfers at 25 MHz. Do not report requested 80 MHz as actual speed.
constexpr uint32_t kSdFrequenciesHz[] = {25000000UL, 20000000UL, 10000000UL, 4000000UL, 1000000UL, 400000UL};
constexpr unsigned long kSdHotplugPollIntervalMs = 2000UL;
constexpr unsigned long kSdRetryBackoffMs[] = {2000UL, 5000UL, 15000UL, 60000UL};

bool flashMounted = false;
bool sdMounted = false;
bool sdSpiStarted = false;
uint32_t sdClockCeiling=25000000UL,sdMountedClock=0;
unsigned long lastSdHotplugPollAt = 0;
unsigned long nextSdMountAttemptAt = 0;
SdSettings activeSdSettings;
// Both bus implementations expose the Arduino filesystem API used below.
template <typename Operation> auto onSd(Operation operation) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && APP_HAS_ONBOARD_PANEL || APP_HAS_CAMERA
    if (activeSdSettings.sdmmc) return operation(SD_MMC);
#endif
    return operation(SD);
}

StorageBackendSummary sdSummaryCache;
portMUX_TYPE storageStateMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t sdWriteDepth = 0;
uint32_t sdReadDepth = 0;
uint8_t sdConsecutiveMountFailures = 0;
bool sdSummaryDirty = false;
bool sdMaintenance = false;
bool sdSettingsPending = false;
bool sdFormatPending = false;
SdFormatState formatState=SdFormatState::Idle;
bool formatPromptDismissed=false;
bool sdEjected=false;int sdMountRequest=0;
SdSettings pendingSdSettings;
TaskHandle_t summaryTask = nullptr;
void (*backgroundWork)(void*)=nullptr;void* backgroundContext=nullptr;

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#if APP_HAS_ONBOARD_PANEL
SPIClass sdSpi(HSPI); // LCD owns SPI2/FSPI on this board.
#else
SPIClass sdSpi(FSPI);
#endif
#elif APP_SUNTON_PANEL
SPIClass sdSpi(VSPI); // LCD uses HSPI; resistive touch uses software SPI.
#else
SPIClass sdSpi(HSPI);
#endif

bool sameSdSettings(const SdSettings& left, const SdSettings& right) {
    return left.sdmmc == right.sdmmc && left.enabled == right.enabled && left.csPin == right.csPin && left.sckPin == right.sckPin &&
        left.mosiPin == right.mosiPin && left.misoPin == right.misoPin;
}

SdSettings effectiveSdSettings(const SdSettings& settings) {
    return settings;
}

bool sdSettingsUsePin(const SdSettings& settings, uint8_t pin) {
    if (!settings.enabled) {
        return false;
    }
#if APP_HAS_CAMERA
    if (settings.sdmmc) return pin == 14 || pin == 15 || pin == 2;
#endif
    return settings.csPin == pin || settings.sckPin == pin || settings.mosiPin == pin || settings.misoPin == pin || (settings.sdmmc && (pin == 15 || pin == 18));
}

unsigned long sdRetryDelayForFailureCount(uint8_t failureCount) {
    if (failureCount == 0) {
        return kSdHotplugPollIntervalMs;
    }

    const size_t maxIndex = (sizeof(kSdRetryBackoffMs) / sizeof(kSdRetryBackoffMs[0])) - 1;
    const size_t index = failureCount > maxIndex ? maxIndex : failureCount - 1;
    return kSdRetryBackoffMs[index];
}

void resetSdMountRetryState() {
    sdConsecutiveMountFailures = 0;
    nextSdMountAttemptAt = 0;
}

bool sdFilesystemHealthy() {
    if (!sdMounted) {
        return false;
    }

    const uint64_t cardBytes = onSd([](auto& card) { return card.cardSize(); });
    if (cardBytes == 0) {
        return false;
    }

    // A cached FAT root may still open after physical removal. SPI cards must
    // answer a real sector read before we declare the mount healthy.
    if(!activeSdSettings.sdmmc){uint8_t sector[512];if(!SD.readRAW(sector,0))return false;}
#if defined(CONFIG_IDF_TARGET_ESP32S3) && APP_HAS_ONBOARD_PANEL || APP_HAS_CAMERA
    else {
        // Arduino 2 SD_MMC uses volume 0 for its own totalBytes/usedBytes API.
        FATFS* filesystem=nullptr;DWORD freeClusters;
        if(f_getfree("0:",&freeClusters,&filesystem)!=FR_OK || !filesystem)return false;
        uint8_t sector[512];if(disk_read(filesystem->pdrv,sector,0,1)!=RES_OK)return false;
    }
#endif
    File root = onSd([](auto& card) { return card.open("/"); });
    const bool healthy = root && root.isDirectory();
    if (root) {
        root.close();
    }
    return healthy;
}

void setSdWriteDepth(uint32_t depth) {
    portENTER_CRITICAL(&storageStateMux);
    sdWriteDepth = depth;
    portEXIT_CRITICAL(&storageStateMux);
}

void incrementSdWriteDepth() {
    portENTER_CRITICAL(&storageStateMux);
    ++sdWriteDepth;
    portEXIT_CRITICAL(&storageStateMux);
}

void decrementSdWriteDepth() {
    portENTER_CRITICAL(&storageStateMux);
    if (sdWriteDepth > 0) {
        --sdWriteDepth;
    }
    portEXIT_CRITICAL(&storageStateMux);
}

bool sdWriteInProgress() {
    portENTER_CRITICAL(&storageStateMux);
    const bool busy = sdWriteDepth > 0;
    portEXIT_CRITICAL(&storageStateMux);
    return busy;
}

void incrementSdReadDepth() {
    portENTER_CRITICAL(&storageStateMux);
    ++sdReadDepth;
    portEXIT_CRITICAL(&storageStateMux);
}

void decrementSdReadDepth() {
    portENTER_CRITICAL(&storageStateMux);
    if (sdReadDepth > 0) {
        --sdReadDepth;
    }
    portEXIT_CRITICAL(&storageStateMux);
}

bool sdReadInProgress() {
    portENTER_CRITICAL(&storageStateMux);
    const bool busy = sdReadDepth > 0;
    portEXIT_CRITICAL(&storageStateMux);
    return busy;
}

void cacheSdSummary(const StorageBackendSummary& summary) {
    portENTER_CRITICAL(&storageStateMux);
    sdSummaryCache = summary;
    portEXIT_CRITICAL(&storageStateMux);
}

StorageBackendSummary cachedSdSummary() {
    portENTER_CRITICAL(&storageStateMux);
    const StorageBackendSummary summary = sdSummaryCache;
    portEXIT_CRITICAL(&storageStateMux);
    return summary;
}

StorageBackendSummary readLiveSdSummary() {
    StorageBackendSummary summary;
    summary.available = activeSdSettings.enabled;
    summary.mounted = sdMounted;
    if (!sdMounted) {
        return summary;
    }

    summary.cardSizeBytes = onSd([](auto& card) { return card.cardSize(); });
    summary.totalBytes = static_cast<uint64_t>(onSd([](auto& card) { return card.totalBytes(); }));
    summary.usedBytes = static_cast<uint64_t>(onSd([](auto& card) { return card.usedBytes(); }));
    summary.freeBytes = summary.totalBytes > summary.usedBytes ? summary.totalBytes - summary.usedBytes : 0;
    return summary;
}

void refreshSdSummaryCache() {
    cacheSdSummary(readLiveSdSummary());
}
void summaryWorker(void*) {
    uint32_t lastSummary=millis();
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        serviceStorageBackends();
        portENTER_CRITICAL(&storageStateMux);
        auto work=backgroundWork;auto context=backgroundContext;backgroundWork=nullptr;backgroundContext=nullptr;
        portEXIT_CRITICAL(&storageStateMux);
        if(work)work(context);
        if(uint32_t(millis()-lastSummary)<30000)continue;
        lastSummary=millis();
        portENTER_CRITICAL(&storageStateMux);
        bool refresh=sdSummaryDirty && !sdMaintenance && sdWriteDepth==0 && sdReadDepth==0;
        if(refresh){sdSummaryDirty=false;++sdReadDepth;}
        portEXIT_CRITICAL(&storageStateMux);
        if(refresh){if(sdMounted)refreshSdSummaryCache();decrementSdReadDepth();}
    }
}

const esp_partition_t* flashFilesystemPartition() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
}

void mountFlashStorage() {
    const esp_partition_t* partition = flashFilesystemPartition();
    if (partition == nullptr) {
        flashMounted = false;
        DebugLog.println("[storage] Flash filesystem disabled by partition table");
        return;
    }

    // Arduino's LittleFS wrapper defaults to the partition label "spiffs".
    // ELMA names this generic data partition "storage", so mount the actual
    // label discovered from the partition table instead of relying on that
    // unrelated default.
    flashMounted = LittleFS.begin(false, "/littlefs", 10, partition->label);
    if (!flashMounted) {
        DebugLog.println("[storage] LittleFS mount failed, attempting format");
        flashMounted = LittleFS.begin(true, "/littlefs", 10, partition->label);
    }

    if (flashMounted) {
        DebugLog.printf("[storage] LittleFS mounted total=%u used=%u\n",
                      static_cast<unsigned>(LittleFS.totalBytes()),
                      static_cast<unsigned>(LittleFS.usedBytes()));
    } else {
        DebugLog.println("[storage] LittleFS mount failed");
    }
}

void unmountSdStorage() {
    if (sdMounted) {
        onSd([](auto& card) { return card.end(); });
        sdMounted = false;
    }
    if (sdSpiStarted) {
        sdSpi.end();
        sdSpiStarted = false;
    }

    StorageBackendSummary summary = cachedSdSummary();
    summary.available = activeSdSettings.enabled;
    summary.mounted = false;
    summary.usedBytes = 0;
    summary.freeBytes = summary.totalBytes;
    cacheSdSummary(summary);
}

// Only called with the filesystem unmounted and the maintenance lease held.
// CMD0 is a bounded, read-only presence probe: absent slots never enter FatFs.
bool spiCardResponds(const SdSettings& settings) {
    sdSpi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    bool present=false;
    for(unsigned attempt=0;attempt<3 && !present;++attempt){
        digitalWrite(settings.csPin,HIGH);
        for(unsigned i=0;i<10;++i)sdSpi.transfer(0xff);
        digitalWrite(settings.csPin,LOW);
        const uint8_t command[]={0x40,0,0,0,0,0x95};
        for(uint8_t value:command)sdSpi.transfer(value);
        for(unsigned i=0;i<9;++i){uint8_t response=sdSpi.transfer(0xff);if(!(response&0x80)){present=response==1;break;}}
        digitalWrite(settings.csPin,HIGH);sdSpi.transfer(0xff);
        if(!present)delay(2);
    }
    sdSpi.endTransaction();
    return present;
}

// Read-only FAT probe distinguishes an absent/I/O-failing card from a responding
// card with no supported filesystem. Never infer permission to erase from failure.
bool spiCardNeedsFormat(const SdSettings& settings) {
    uint8_t drive=sdcard_init(settings.csPin,&sdSpi,1000000);
    if(drive==0xff)return false;
    // FATFS contains a sector buffer up to FF_MAX_SS (4096 on this SDK).
    // It must not live on the 4096-byte storage worker stack.
    std::unique_ptr<FATFS> filesystem(allocatePreferPsram<FATFS>());
    if(!filesystem){sdcard_uninit(drive);return false;}
    char name[]={char('0'+drive),':',0};
    FRESULT result=f_mount(filesystem.get(),name,1);
    f_mount(nullptr,name,0);sdcard_uninit(drive);
    return result==FR_NO_FILESYSTEM;
}

#if defined(CONFIG_IDF_TARGET_ESP32S3) && APP_HAS_ONBOARD_PANEL || APP_HAS_CAMERA
bool mmcCardNeedsFormat(bool format=false) {
    sdmmc_host_t host=SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();
    host.slot=SDMMC_HOST_SLOT_1;host.max_freq_khz=SDMMC_FREQ_DEFAULT;
#if APP_HAS_CAMERA
    host.flags=SDMMC_HOST_FLAG_1BIT;slot.width=1;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    // Camera boards using SPI never enter this path. Do not guess SDMMC pins.
    return false;
#endif
#else
    host.flags=SDMMC_HOST_FLAG_4BIT;slot.width=4;
    slot.clk=GPIO_NUM_14;slot.cmd=GPIO_NUM_17;slot.d0=GPIO_NUM_16;
    slot.d1=GPIO_NUM_18;slot.d2=GPIO_NUM_15;slot.d3=GPIO_NUM_21;
#endif
    if(sdmmc_host_init()!=ESP_OK)return false;
    sdmmc_card_t card{};bool needsFormat=false;
    if(sdmmc_host_init_slot(host.slot,&slot)==ESP_OK && sdmmc_card_init(&host,&card)==ESP_OK){
        BYTE drive;
        if(ff_diskio_get_drive(&drive)==ESP_OK){
            ff_diskio_register_sdmmc(drive,&card);
            std::unique_ptr<FATFS> filesystem(allocatePreferPsram<FATFS>());
            char name[]={char('0'+drive),':',0};
            needsFormat=filesystem && f_mount(filesystem.get(),name,1)==FR_NO_FILESYSTEM;
            // Formatting is permitted only for this positively identified FAT
            // result, not a timeout, bad wiring, or general mount error.
            if(needsFormat && format){std::unique_ptr<BYTE[]> work(new(std::nothrow) BYTE[FF_MAX_SS]);if(work)f_mkfs(name,FM_ANY,0,work.get(),FF_MAX_SS);}
            f_mount(nullptr,name,0);ff_diskio_unregister(drive);
        }
    }
    sdmmc_host_deinit();return needsFormat;
}
#endif

bool mountSdStorage(const SdSettings& settings, bool allowFormat=false) {
    portENTER_CRITICAL(&storageStateMux);
    const bool busy=sdMaintenance || sdWriteDepth || sdReadDepth;
    if(!busy)sdMaintenance=true;
    portEXIT_CRITICAL(&storageStateMux);
    if(busy){portENTER_CRITICAL(&storageStateMux);pendingSdSettings=settings;sdSettingsPending=true;portEXIT_CRITICAL(&storageStateMux);return false;}
    struct Release {~Release(){portENTER_CRITICAL(&storageStateMux);sdMaintenance=false;portEXIT_CRITICAL(&storageStateMux);}} release;
    if(!sameSdSettings(activeSdSettings,settings))sdClockCeiling=kSdFrequenciesHz[0];
    unmountSdStorage();
    activeSdSettings = settings;
    lastSdHotplugPollAt = millis();

    if (!settings.enabled) {
        resetSdMountRetryState();
        cacheSdSummary(StorageBackendSummary{});
        return true;
    }

#if defined(CONFIG_IDF_TARGET_ESP32S3) && APP_HAS_ONBOARD_PANEL || APP_HAS_CAMERA
    if (settings.sdmmc) {
        if(allowFormat)mmcCardNeedsFormat(true);
#if APP_HAS_CAMERA
        sdMounted = SD_MMC.begin("/sd", true, false, 20000, 5);
#else
        SD_MMC.setPins(14, 17, 16, 18, 15, 21);
        sdMounted = SD_MMC.begin("/sd", false, false, 20000, 5);
#endif
        if (sdMounted) {
            resetSdMountRetryState();
            refreshSdSummaryCache();
        } else {
            SD_MMC.end();
            sdConsecutiveMountFailures=min<uint8_t>(254,sdConsecutiveMountFailures)+1;
            nextSdMountAttemptAt = millis() + sdRetryDelayForFailureCount(sdConsecutiveMountFailures);
            StorageBackendSummary summary;
            summary.available = true;
            summary.needsFormat = mmcCardNeedsFormat();
            cacheSdSummary(summary);
            DebugLog.println("[storage] SDMMC mount failed");
        }
        return sdMounted;
    }
#endif
    pinMode(settings.csPin, OUTPUT);
    digitalWrite(settings.csPin, HIGH);
    sdSpi.begin(settings.sckPin, settings.misoPin, settings.mosiPin, settings.csPin);
    sdSpiStarted = true;

    if(!spiCardResponds(settings)){
        if(sdConsecutiveMountFailures==0)DebugLog.println("[storage] SD slot empty or card not responding; waiting for insertion");
        sdConsecutiveMountFailures=min<uint8_t>(254,sdConsecutiveMountFailures)+1;
        nextSdMountAttemptAt=millis()+5000;
        StorageBackendSummary summary;summary.available=true;
        cacheSdSummary(summary); // Not a format request: no responding medium.
        return false;
    }

    // Re-check at execution time. SD.begin(true) only formats FR_NO_FILESYSTEM;
    // an already valid filesystem is never reformatted.
    if(allowFormat && !spiCardNeedsFormat(settings))allowFormat=false;

    for (uint32_t frequencyHz : kSdFrequenciesHz) {
        if(frequencyHz>sdClockCeiling&&!allowFormat)continue;
        if(allowFormat)frequencyHz=1000000; // Conservative clock for destructive writes.
        bool began=SD.begin(settings.csPin, sdSpi, frequencyHz, "/sd", 5, allowFormat);
        allowFormat=false; // A confirmation authorizes one format attempt only.
        if (!began) {
            DebugLog.printf("[storage] SD begin failed cs=%u sck=%u mosi=%u miso=%u freq=%lu\n",
                          static_cast<unsigned>(settings.csPin),
                          static_cast<unsigned>(settings.sckPin),
                          static_cast<unsigned>(settings.mosiPin),
                          static_cast<unsigned>(settings.misoPin),
                          static_cast<unsigned long>(frequencyHz));
            continue;
        }

        const uint64_t cardBytes = onSd([](auto& card) { return card.cardSize(); });
        const size_t totalBytes = onSd([](auto& card) { return card.totalBytes(); });
        if (cardBytes == 0 || totalBytes == 0) {
            DebugLog.printf("[storage] SD detected but filesystem unavailable cs=%u sck=%u mosi=%u miso=%u freq=%lu card=%llu total=%u\n",
                          static_cast<unsigned>(settings.csPin),
                          static_cast<unsigned>(settings.sckPin),
                          static_cast<unsigned>(settings.mosiPin),
                          static_cast<unsigned>(settings.misoPin),
                          static_cast<unsigned long>(frequencyHz),
                          static_cast<unsigned long long>(cardBytes),
                          static_cast<unsigned>(totalBytes));
            onSd([](auto& card) { return card.end(); });
            continue;
        }

        // A mounted FAT cache alone cannot validate the transfer rate.
        uint8_t sector[512];bool verified=true;
        for(uint32_t block:{0UL,1UL,2UL})if(!SD.readRAW(sector,block)){verified=false;break;}
        if(!verified){DebugLog.printf("[storage] SD sector test failed at %lu Hz; lowering clock\n",static_cast<unsigned long>(frequencyHz));SD.end();continue;}
        sdMountedClock=frequencyHz;sdClockCeiling=frequencyHz;
        sdMounted = true;
        resetSdMountRetryState();
        DebugLog.printf("[storage] SD mounted cs=%u sck=%u mosi=%u miso=%u freq=%lu card=%llu total=%u used=%u\n",
                      static_cast<unsigned>(settings.csPin),
                      static_cast<unsigned>(settings.sckPin),
                      static_cast<unsigned>(settings.mosiPin),
                      static_cast<unsigned>(settings.misoPin),
                      static_cast<unsigned long>(frequencyHz),
                      static_cast<unsigned long long>(cardBytes),
                      static_cast<unsigned>(totalBytes),
                      static_cast<unsigned>(onSd([](auto& card) { return card.usedBytes(); })));
        refreshSdSummaryCache();
        break;
    }

    if (!sdMounted) {
        sdConsecutiveMountFailures=min<uint8_t>(254,sdConsecutiveMountFailures)+1;
        const unsigned long retryDelayMs = sdRetryDelayForFailureCount(sdConsecutiveMountFailures);
        nextSdMountAttemptAt = millis() + retryDelayMs;
        DebugLog.printf("[storage] SD mount failed cs=%u sck=%u mosi=%u miso=%u retry_in=%lu failure=%u\n",
                      static_cast<unsigned>(settings.csPin),
                      static_cast<unsigned>(settings.sckPin),
                      static_cast<unsigned>(settings.mosiPin),
                      static_cast<unsigned>(settings.misoPin),
                      static_cast<unsigned long>(retryDelayMs),
                      static_cast<unsigned>(sdConsecutiveMountFailures));
        StorageBackendSummary summary;
        summary.available = settings.enabled;
        summary.needsFormat = spiCardNeedsFormat(settings);
        cacheSdSummary(summary);
    }
    return sdMounted;
}
}  // namespace

void beginStorageBackends(const SettingsBundle& settings) {
    mountFlashStorage();
    mountSdStorage(effectiveSdSettings(settings.sd));
    if(!summaryTask)xTaskCreate(summaryWorker,"storage-summary",4096,nullptr,1,&summaryTask);
}

void applyStorageSettings(const SettingsBundle& settings) {
    const SdSettings effective=effectiveSdSettings(settings.sd);
    // Reconfiguration and retry probing run in the maintenance task, never
    // in the ESP main loop. Active reads/writes postpone the change.
    portENTER_CRITICAL(&storageStateMux);
    pendingSdSettings=effective;sdSettingsPending=true;
    portEXIT_CRITICAL(&storageStateMux);
}

bool remountActiveStorageBackend(StorageTarget target) {
    if (target == StorageTarget::Flash) {
        if (flashMounted) {
            LittleFS.end();
            flashMounted = false;
        }
        mountFlashStorage();
        return flashMounted;
    }

    if (sdStorageEjected() || sdWriteInProgress() || sdReadInProgress()) {
        return false;
    }

    return mountSdStorage(activeSdSettings);
}

bool remountStorageBackend(StorageTarget target, const SettingsBundle& settings) {
    if (target == StorageTarget::Flash) {
        if (flashMounted) {
            LittleFS.end();
            flashMounted = false;
        }
        mountFlashStorage();
        return flashMounted;
    }

    if (sdStorageEjected() || sdWriteInProgress() || sdReadInProgress()) {
        return false;
    }

    return mountSdStorage(effectiveSdSettings(settings.sd));
}

void pollStorageBackends() {
    // Retry task allocation without falling back to synchronous card probing.
    static uint32_t lastAttempt=0;
    if(!summaryTask&&uint32_t(millis()-lastAttempt)>=30000){lastAttempt=millis();if(xTaskCreate(summaryWorker,"storage-summary",4096,nullptr,1,&summaryTask)!=pdPASS)DebugLog.println("[storage] Maintenance task memory unavailable");}
}
bool requestStorageBackgroundJob(void (*work)(void*),void* context){
    portENTER_CRITICAL(&storageStateMux);bool accepted=summaryTask&&!backgroundWork;
    if(accepted){backgroundWork=work;backgroundContext=context;}
    portEXIT_CRITICAL(&storageStateMux);return accepted;
}
bool requestSdFormat(bool confirmed,String& error) {
    portENTER_CRITICAL(&storageStateMux);
    bool permitted=confirmed && !formatPromptDismissed && formatState!=SdFormatState::Formatting && sdSummaryCache.needsFormat && !sdMounted &&
        !sdMaintenance && !sdReadDepth && !sdWriteDepth && !sdSettingsPending && !sdFormatPending && !sdMountRequest && !sdEjected && summaryTask;
    if(permitted){sdFormatPending=true;formatState=SdFormatState::Pending;formatPromptDismissed=true;}
    portEXIT_CRITICAL(&storageStateMux);
    if(!permitted)error="Formatting unavailable: confirm an unreadable card, or wait for storage to become idle";
    return permitted;
}
SdFormatState sdFormatState(){portENTER_CRITICAL(&storageStateMux);auto state=formatState;portEXIT_CRITICAL(&storageStateMux);return state;}
bool sdFormatPromptNeeded(){portENTER_CRITICAL(&storageStateMux);bool show=sdSummaryCache.needsFormat&&!sdMounted&&!formatPromptDismissed;portEXIT_CRITICAL(&storageStateMux);return show;}
void dismissSdFormatPrompt(){portENTER_CRITICAL(&storageStateMux);formatPromptDismissed=true;portEXIT_CRITICAL(&storageStateMux);}
bool sdStorageEjected(){portENTER_CRITICAL(&storageStateMux);bool value=sdEjected;portEXIT_CRITICAL(&storageStateMux);return value;}
bool requestSdMount(bool mount,String& error){
    portENTER_CRITICAL(&storageStateMux);
    bool ok=activeSdSettings.enabled && summaryTask && formatState!=SdFormatState::Formatting && !sdMaintenance && !sdReadDepth && !sdWriteDepth && !sdFormatPending && !sdSettingsPending && !sdMountRequest;
    if(ok)sdMountRequest=mount?1:-1;
    portEXIT_CRITICAL(&storageStateMux);
    if(!ok)error="SD card busy or unavailable. Stop playback and transfers first.";
    return ok;
}
void serviceStorageBackends() {
    portENTER_CRITICAL(&storageStateMux);
    int mountRequest=(!sdMaintenance&&!sdReadDepth&&!sdWriteDepth)?sdMountRequest:0;
    if(mountRequest){sdMountRequest=0;sdEjected=mountRequest<0;if(mountRequest<0)sdMaintenance=true;}
    bool format=!mountRequest&&sdFormatPending&&!sdMaintenance&&!sdReadDepth&&!sdWriteDepth;
    if(format){sdFormatPending=false;formatState=SdFormatState::Formatting;}
    bool apply=!mountRequest&&!format&&sdSettingsPending&&!sdMaintenance&&!sdReadDepth&&!sdWriteDepth;
    SdSettings requested=pendingSdSettings;
    if(apply)sdSettingsPending=false;
    portEXIT_CRITICAL(&storageStateMux);
    if(mountRequest<0){unmountSdStorage();portENTER_CRITICAL(&storageStateMux);sdMaintenance=false;formatPromptDismissed=true;portEXIT_CRITICAL(&storageStateMux);return;}
    if(mountRequest>0){mountSdStorage(activeSdSettings);return;}
    if(format){bool ok=mountSdStorage(activeSdSettings,true);portENTER_CRITICAL(&storageStateMux);formatState=ok?SdFormatState::Complete:SdFormatState::Failed;portEXIT_CRITICAL(&storageStateMux);return;}
    if(apply&&!sameSdSettings(activeSdSettings,requested))mountSdStorage(requested);
    if (!activeSdSettings.enabled || sdStorageEjected()) {
        return;
    }

    if (sdWriteInProgress() || sdReadInProgress()) {
        return;
    }

    const unsigned long now = millis();
    if (static_cast<unsigned long>(now - lastSdHotplugPollAt) < kSdHotplugPollIntervalMs) {
        return;
    }
    lastSdHotplugPollAt = now;

    if (sdMounted) {
        portENTER_CRITICAL(&storageStateMux);
        bool check=!sdMaintenance&&!sdReadDepth&&!sdWriteDepth;
        if(check)++sdReadDepth;
        portEXIT_CRITICAL(&storageStateMux);
        if(!check)return;
        bool healthy=sdFilesystemHealthy();decrementSdReadDepth();
        if(!healthy){
            if(!activeSdSettings.sdmmc){for(uint32_t hz:kSdFrequenciesHz)if(hz<sdMountedClock){sdClockCeiling=hz;break;}}
            DebugLog.printf("[storage] SD unavailable; retrying with SPI ceiling %lu Hz\n",static_cast<unsigned long>(sdClockCeiling));mountSdStorage(activeSdSettings);
        }
        return;
    }

    if (nextSdMountAttemptAt != 0 && static_cast<long>(now - nextSdMountAttemptAt) < 0) {
        return;
    }

    mountSdStorage(activeSdSettings);
}

StorageTarget parseStorageTarget(const String& rawTarget) {
    String target = rawTarget;
    target.trim();
    target.toLowerCase();
    return target == "sd" ? StorageTarget::Sd : StorageTarget::Flash;
}

const char* storageTargetId(StorageTarget target) {
    return target == StorageTarget::Sd ? "sd" : "flash";
}

const char* storageTargetLabel(StorageTarget target) {
    return target == StorageTarget::Sd ? "SD card" : "Flash filesystem";
}

StorageBackendSummary getStorageSummary(StorageTarget target) {
    StorageBackendSummary summary;

    if (target == StorageTarget::Flash) {
        const esp_partition_t* partition = flashFilesystemPartition();
        summary.available = partition != nullptr;
        summary.cardSizeBytes = partition != nullptr ? static_cast<uint64_t>(partition->size) : 0;
        summary.totalBytes = summary.cardSizeBytes;
        summary.freeBytes = summary.totalBytes;

        const size_t mountedTotal = LittleFS.totalBytes();
        if (mountedTotal > 0) {
            summary.mounted = true;
            summary.totalBytes = static_cast<uint64_t>(mountedTotal);
            summary.usedBytes = static_cast<uint64_t>(LittleFS.usedBytes());
            summary.freeBytes = summary.totalBytes > summary.usedBytes ? summary.totalBytes - summary.usedBytes : 0;
        }
        return summary;
    }

    summary.available = activeSdSettings.enabled;
    summary = cachedSdSummary();
    summary.available = activeSdSettings.enabled;
    summary.mounted = storageMounted(StorageTarget::Sd);
    if (!summary.mounted) {
        summary.usedBytes = 0;
        summary.freeBytes = summary.totalBytes;
    }
    return summary;
}

void beginStorageWrite(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        incrementSdWriteDepth();
    }
}

void endStorageWrite(StorageTarget target) {
    if (target != StorageTarget::Sd) {
        return;
    }

    decrementSdWriteDepth();
    // Never scan the FAT from a writer or network callback. Summary refresh
    // is deferred to the idle maintenance worker.
    portENTER_CRITICAL(&storageStateMux);
    sdSummaryDirty = true;
    portEXIT_CRITICAL(&storageStateMux);
}

void beginStorageRead(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        incrementSdReadDepth();
    }
}

void endStorageRead(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        decrementSdReadDepth();
    }
}

bool storageBusy(StorageTarget target) {
    if(target!=StorageTarget::Sd)return false;
    portENTER_CRITICAL(&storageStateMux);bool busy=sdMaintenance||sdWriteDepth||sdReadDepth;portEXIT_CRITICAL(&storageStateMux);return busy;
}

fs::FS* getStorageFs(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        return storageMounted(StorageTarget::Sd) ? onSd([](auto& card) { return static_cast<fs::FS*>(&card); }) : nullptr;
    }
    return LittleFS.totalBytes() > 0 ? static_cast<fs::FS*>(&LittleFS) : nullptr;
}

bool storageMounted(StorageTarget target) {
    if (target == StorageTarget::Sd) {
        portENTER_CRITICAL(&storageStateMux);bool ready=sdMounted&&!sdMaintenance&&!sdEjected;portEXIT_CRITICAL(&storageStateMux);return ready;
    }
    return LittleFS.totalBytes() > 0;
}

bool storageConfigured(StorageTarget target) {
    return target == StorageTarget::Sd ? activeSdSettings.enabled : flashFilesystemPartition() != nullptr;
}

bool storageExists(StorageTarget target, const String& path) {
    beginStorageRead(target);fs::FS* fs=getStorageFs(target);
    bool exists=fs&&fs->exists(path);endStorageRead(target);return exists;
}

File storageOpen(StorageTarget target, const String& path, const char* mode) {
    fs::FS* fs = getStorageFs(target);
    if (fs == nullptr) {
        return File();
    }
    return fs->open(path, mode);
}

bool storageRemove(StorageTarget target, const String& path) {
    beginStorageWrite(target);fs::FS* fs=getStorageFs(target);
    bool removed=fs&&fs->remove(path);endStorageWrite(target);return removed;
}

bool sdStorageUsesPin(uint8_t pin) {
    return sdSettingsUsePin(activeSdSettings, pin);
}

#endif
