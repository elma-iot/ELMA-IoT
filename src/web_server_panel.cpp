#include "web_server.h"
#if APP_HAS_ONBOARD_PANEL && !defined(APP_DISABLE_WEB_UI)
#include "panel_settings.h"
#include "storage_memory.h"
#include "panel_memory.h"
#include "system_metrics.h"
#include "device_log.h"
#include "plot_telemetry.h"
#include "version.h"
#include "panel_radio.h"
#include "bno055_service.h"
#include "rs485_service.h"
#include <memory>
namespace {
portMUX_TYPE browseMux=portMUX_INITIALIZER_UNLOCKED;
struct PanelBrowseJob{StorageTarget target;String path,result;int offset;bool done=false;};
void browseStorage(void* context){
 auto* job=static_cast<PanelBrowseJob*>(context);JsonDocument listing;auto files=listing["files"].to<JsonArray>();
 beginStorageRead(job->target);File dir=storageOpen(job->target,job->path);
 if(!dir||!dir.isDirectory())listing["error"]="Folder unavailable. Insert a card or return to root.";
 else for(int i=0;dir;i++){
  File file=dir.openNextFile();if(!file)break;
  if(i<job->offset){file.close();continue;}
  if(files.size()>=12){listing["more"]=true;file.close();break;}
  String name=file.name();name=name.substring(name.lastIndexOf('/')+1);auto item=files.add<JsonObject>();item["name"]=name;item["path"]=(job->path=="/"?String("/"):job->path+"/")+name;item["directory"]=file.isDirectory();item["size"]=file.size();file.close();
  taskYIELD();
 }
 dir.close();endStorageRead(job->target);
 if(listing.overflowed()){listing.clear();listing["error"]="Not enough memory for folder listing";}
 serializeJson(listing,job->result);portENTER_CRITICAL(&browseMux);job->done=true;portEXIT_CRITICAL(&browseMux);
}
bool selected(JsonVariantConst values,const char* fragment=nullptr){
 if(values.is<const char*>()){String s=values.as<String>();return s.length()&&s!="none"&&(!fragment||s.indexOf(fragment)>=0);}
 for(JsonVariantConst v:values.as<JsonArrayConst>())if(selected(v,fragment))return true;return false;
}
}
void WebServerManager::panelSnapshot(const String& requestedPage,JsonObject root){
 const bool round=requestedPage.startsWith("round/");
 const String page=round?requestedPage.substring(6):requestedPage;
 if(panelBrowseJob_&&!page.startsWith("storage-")){auto* job=static_cast<PanelBrowseJob*>(panelBrowseJob_);portENTER_CRITICAL(&browseMux);bool done=job->done;portEXIT_CRITICAL(&browseMux);if(done){delete job;panelBrowseJob_=nullptr;panelStorageDirty_=true;}}

 if(page=="info"){root["build"]=__DATE__ " " __TIME__;root["mac"]=WiFi.macAddress();}
 root["version"]=APP_VERSION;security_.status(root["security"].to<JsonObject>());
 if(security_.locked()&&!round)return;
 root["sdNeedsFormat"]=sdFormatPromptNeeded();
 if(page=="firmware")otaManager_->appendStatusJson(root["ota"].to<JsonObject>());
 std::unique_ptr<SettingsBundle> snapshot(new(std::nothrow) SettingsBundle(settingsGetter_()));if(!snapshot)return;const auto& settings=*snapshot;auto config=root["settings"].to<JsonObject>();
 const char* section=page=="wifi"?"wifi":page=="mqtt"?"mqtt":page=="device"||page=="gpio"?"device":page=="oled"?"oled":page=="battery"?"battery":page=="effects"?"effects":page=="playback"||page.startsWith("storage-")?"audio":page=="firmware"?"ota":nullptr;
 if(section)settingsManager_->toJson(settings,config,section,false);
 if(round&&page=="security")settingsManager_->toJson(settings,config,"oled",false);
 if(page=="gpio"||page=="wled"||page=="motor")settingsManager_->toJson(settings,config,"ui",false);
 // Passwords stay on the unlocked local panel; other pages never carry them.
 if(page!="mqtt")config["mqtt"].remove("password");config["webAuth"].remove("password");
 JsonDocument profileDoc(panelJsonAllocator());JsonVariantConst profiles=config["ui"]["peripheralProfiles"];
 if(profiles.isNull()){deserializeJson(profileDoc,settings.ui.peripheralProfileSelections);profiles=profileDoc.as<JsonVariantConst>();}
 if(page=="wifi"){
  root["wifiLive"]["ssid"]=wifiManager_->currentSsid();
  auto scan=wifiManager_->getScanSnapshot();auto result=root["wifiScan"].to<JsonObject>();
  result["scanning"]=scan.active;result["complete"]=scan.complete;result["failed"]=scan.failed;
  if(scan.complete)wifiManager_->appendScanResultsJson(result["networks"].to<JsonArray>());
 }
 auto caps=root["caps"].to<JsonObject>();
 caps["rs485"]=Rs485::available();if(page=="rs485")Rs485::snapshot(root["rs485"].to<JsonObject>(),false);
 caps["bno055"]=selected(profiles["sensors"],"bno055");if(page=="bno055")Bno055::snapshot(root["bno055"].to<JsonObject>());
 caps["wled"]=selected(profiles["controls"],"ws2812");caps["motor"]=selected(profiles["controls"],"motor-driver");
 caps["playback"]=settings.audio.enabled;caps["effects"]=caps["playback"];
 caps["battery"]=selected(profiles["sensors"],"voltage-divider")&&settings.battery.adcPin>0;
 caps["oled"]=selected(profiles["displayProfiles"])||selected(profiles["displayProfile"])||settings.oled.enabled;
 // Slot capability is independent of whether a card is inserted or mounted.
 caps["storage-external"]=settings.sd.enabled||selected(profiles["storage"]);caps["migration"]=false;
 static bool hasPlots=false;static uint32_t graphAt=0;
 if(page=="logics"||page=="plots"||millis()-graphAt>5000){
  JsonDocument graph(panelJsonAllocator());if(logicsGetter_)logicsGetter_(graph,page=="logics"&&!round);
  if(page=="logics"||page=="plots")root["logics"].set(graph);
  hasPlots=graph["hasPlots"]|false;
  if(!graph["graph"].isNull()){hasPlots=false;for(JsonObjectConst n:graph["graph"]["nodes"].as<JsonArrayConst>())if(n["type"]=="mainboard.plot")hasPlots=true;}
  graphAt=millis();
 }caps["plots"]=hasPlots;
 if(page=="wled"&&ledStatusAppender_)ledStatusAppender_(root);
 if(page=="motor"&&motorStatusAppender_)motorStatusAppender_(root);
 if(page=="hardware"||page=="info"||page=="gpio")appendSystemMetricsJson(root);
 if(page=="firmware")otaManager_->appendStatusJson(root);
 if(page=="logs"){JsonDocument log(panelJsonAllocator());DebugLog.snapshot(log.to<JsonObject>(),"");String text=log["text"]|"";root["logText"]=text.substring(text.length()>3000?text.length()-3000:0);}
 if(page=="plots"){JsonDocument samples(panelJsonAllocator());plotSamplesSince(0,0,samples);root["plots"].set(samples);}
 if(page=="playback")PanelRadio::snapshot(root["radio"].to<JsonObject>());
 if(page=="storage-internal"||page=="storage-external"){
  StorageTarget target=page=="storage-external"?StorageTarget::Sd:StorageTarget::Flash;auto status=getStorageSummary(target);root["storage"]["mounted"]=status.mounted;root["storage"]["total"]=status.totalBytes;root["storage"]["free"]=status.freeBytes;
  if(panelStoragePage_!=page){panelStoragePage_=page;panelStoragePath_="/";panelStorageOffset_=0;panelStorageDirty_=true;}
  if(panelStorageMounted_!=status.mounted){panelStorageMounted_=status.mounted;panelStorageDirty_=true;}
  if(panelBrowseJob_){auto* job=static_cast<PanelBrowseJob*>(panelBrowseJob_);portENTER_CRITICAL(&browseMux);bool done=job->done;portEXIT_CRITICAL(&browseMux);
   if(done){if(job->target==target&&job->path==panelStoragePath_&&job->offset==panelStorageOffset_&&!panelStorageDirty_)panelStorageListing_=job->result;delete job;panelBrowseJob_=nullptr;}
  }
  if(panelStorageDirty_&&!panelBrowseJob_){
   auto* job=new(std::nothrow) PanelBrowseJob{target,panelStoragePath_,"",panelStorageOffset_,false};
   if(job&&requestStorageBackgroundJob(browseStorage,job)){panelBrowseJob_=job;panelStorageDirty_=false;panelStorageListing_="";}else delete job;
  }
  root["storage"]["busy"]=panelBrowseJob_!=nullptr;
  JsonDocument listing(panelJsonAllocator());deserializeJson(listing,panelStorageListing_);
  root["files"].set(listing["files"]);root["storage"]["error"]=listing["error"]|"";root["storage"]["more"]=listing["more"]|false;
  root["storage"]["path"]=panelStoragePath_;root["storage"]["offset"]=panelStorageOffset_;
 }
}
bool WebServerManager::panelCommand(const String& action,JsonVariantConst args,String& error){
 if(action=="security"){JsonDocument result;int code=security_.command(args,result.to<JsonObject>());error=result["error"]|"";return code==200;}
 if(security_.locked()){error="Unlock the interface first";return false;}
 {JsonDocument activity,result;activity["action"]="activity";security_.command(activity,result.to<JsonObject>());}
 if(action=="patch"){
  // Resolve against current settings here, not the older screen snapshot.
  JsonDocument latest(panelJsonAllocator()),patch(panelJsonAllocator());std::unique_ptr<SettingsBundle> currentStorage(new(std::nothrow) SettingsBundle(settingsGetter_()));if(!currentStorage){error="Not enough memory to apply settings";return false;}const auto& current=*currentStorage;auto latestRoot=latest.to<JsonObject>();
  for(JsonObjectConst change:args["changes"].as<JsonArrayConst>()){
   std::string path=change["path"]|"";size_t split=path.find('/');if(split==std::string::npos){error="Invalid setting";return false;}
   const std::string section=path.substr(0,split);if(patch[section].isNull()){settingsManager_->toJson(current,latestRoot,section.c_str());if(latest.overflowed()){error="Not enough memory to apply settings";return false;}patch[section].set(latest[section]);}
   if(!PanelSettings::set(patch.as<JsonVariant>(),path,change["value"])){error="Invalid setting";return false;}
  }
  if(patch.overflowed()){error="Not enough memory to apply settings";return false;}
  return settingsSaver_(patch,error);
 }
 if(action=="logics"){JsonDocument result;return logicsHandler_&&logicsHandler_(args,result,error);}
 if(action=="rs485")return Rs485::command(args,error);
 if(action=="bno055")return Bno055::command(args,error);
 if(action=="wifiScan"){if(wifiManager_->startScan())return true;error="Wi-Fi scan could not start; try again";return false;}
 if(action=="radio")return PanelRadio::request(args,error);
 if(action=="formatSdPrompt")return requestSdFormatPrompt(error);
 if(action=="formatSd")return requestSdFormat(args["confirmed"]|false,error,args["filesystem"]|"FAT32");
 if(action=="dismissSdFormat"){dismissSdFormatPrompt();return true;}
 if(action=="mountSd")return requestSdMount(true,error);
 if(action=="ejectSd")return requestSdMount(false,error);
 if(action=="browse"){
  String path=args["path"]|"/";
  if(!path.startsWith("/")||path.indexOf("..")>=0||path.length()>240){error="Invalid folder path";return false;}
  panelStoragePath_=path;panelStorageOffset_=max(0,args["offset"]|0);panelStorageDirty_=true;return true;
 }
 if(action=="volume"){if(volumeHandler_)volumeHandler_(constrain(args["value"]|0,0,100));return true;}
 if(action=="stop"){if(stopHandler_)stopHandler_();return true;}
 if(action=="play")return playHandler_&&playHandler_(args["url"]|"",args["label"]|"",args["type"]|"stream",error);
 if(action=="seek")return seekHandler_&&seekHandler_(args["value"]|0);
 if(action=="mqtt")return mqttHandler_&&mqttHandler_(args["action"]|"",error);
 if(action=="motor")return motorRunHandler_&&motorRunHandler_(args["channel"]|0,args["forward"]|true,args["durationMs"]|0,args["limitInputIndex"]|-1,error);
 if(action=="otaCheck")return otaHandler_&&otaHandler_(false);
 if(action=="otaInstall")return otaManager_->triggerInstallSelected(error);
 if(action=="reboot"){if(rebootHandler_)rebootHandler_();return true;}
 error="Control is unavailable";return false;
}
#endif
