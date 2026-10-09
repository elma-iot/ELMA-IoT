#include "web_server.h"
#if APP_HAS_ONBOARD_PANEL && !defined(APP_DISABLE_WEB_UI)
#include "panel_settings.h"
#include "storage_memory.h"
#include "system_metrics.h"
#include "device_log.h"
#include "plot_telemetry.h"
#include "version.h"
#include "panel_radio.h"
#include "bno055_service.h"
namespace {
bool selected(JsonVariantConst values,const char* fragment=nullptr){
 if(values.is<const char*>()){String s=values.as<String>();return s.length()&&s!="none"&&(!fragment||s.indexOf(fragment)>=0);}
 for(JsonVariantConst v:values.as<JsonArrayConst>())if(selected(v,fragment))return true;return false;
}
}
void WebServerManager::panelSnapshot(const String& page,JsonObject root){
 if(page=="info"){root["build"]=__DATE__ " " __TIME__;root["mac"]=WiFi.macAddress();}
 root["version"]=APP_VERSION;security_.status(root["security"].to<JsonObject>());
 if(security_.locked())return;
 root["sdNeedsFormat"]=sdFormatPromptNeeded();
 SettingsBundle settings=settingsGetter_();auto config=root["settings"].to<JsonObject>();
 const char* section=page=="wifi"?"wifi":page=="mqtt"?"mqtt":page=="device"||page=="gpio"?"device":page=="oled"?"oled":page=="battery"?"battery":page=="effects"?"effects":page=="playback"||page.startsWith("storage-")?"audio":page=="firmware"?"ota":nullptr;
 if(section)settingsManager_->toJson(settings,config,section,false);
 if(page=="gpio"||page=="wled"||page=="motor")settingsManager_->toJson(settings,config,"ui",false);
 // Passwords stay on the unlocked local panel; other pages never carry them.
 if(page!="mqtt")config["mqtt"].remove("password");config["webAuth"].remove("password");
 JsonDocument profileDoc(storageJsonAllocator());JsonVariantConst profiles=config["ui"]["peripheralProfiles"];
 if(profiles.isNull()){deserializeJson(profileDoc,settings.ui.peripheralProfileSelections);profiles=profileDoc.as<JsonVariantConst>();}
 if(page=="wifi"){
  root["wifiLive"]["ssid"]=wifiManager_->currentSsid();
  auto scan=wifiManager_->getScanSnapshot();auto result=root["wifiScan"].to<JsonObject>();
  result["scanning"]=scan.active;result["complete"]=scan.complete;result["failed"]=scan.failed;
  if(scan.complete)wifiManager_->appendScanResultsJson(result["networks"].to<JsonArray>());
 }
 auto caps=root["caps"].to<JsonObject>();
 caps["bno055"]=selected(profiles["sensors"],"bno055");if(page=="bno055")Bno055::snapshot(root["bno055"].to<JsonObject>());
 caps["wled"]=selected(profiles["controls"],"ws2812");caps["motor"]=selected(profiles["controls"],"motor-driver");
 caps["playback"]=settings.audio.enabled||selected(profiles["audioProfiles"])||selected(profiles["audioProfile"]);caps["effects"]=caps["playback"];
 caps["battery"]=selected(profiles["sensors"],"voltage-divider")&&settings.battery.adcPin>0;
 caps["oled"]=selected(profiles["displayProfiles"])||selected(profiles["displayProfile"])||settings.oled.enabled;
 // Slot capability is independent of whether a card is inserted or mounted.
 caps["storage-external"]=settings.sd.enabled||selected(profiles["storage"]);caps["migration"]=false;
 static bool hasPlots=false;static uint32_t graphAt=0;
 if(page=="logics"||page=="plots"||millis()-graphAt>5000){
  JsonDocument graph(storageJsonAllocator());if(logicsGetter_)logicsGetter_(graph,page=="logics");
  if(page=="logics"||page=="plots")root["logics"].set(graph);
  hasPlots=graph["hasPlots"]|false;
  if(!graph["graph"].isNull()){hasPlots=false;for(JsonObjectConst n:graph["graph"]["nodes"].as<JsonArrayConst>())if(n["type"]=="mainboard.plot")hasPlots=true;}
  graphAt=millis();
 }caps["plots"]=hasPlots;
 if(page=="wled"&&ledStatusAppender_)ledStatusAppender_(root);
 if(page=="motor"&&motorStatusAppender_)motorStatusAppender_(root);
 if(page=="hardware"||page=="info"||page=="gpio")appendSystemMetricsJson(root);
 if(page=="firmware")otaManager_->appendStatusJson(root);
 if(page=="logs"){JsonDocument log(storageJsonAllocator());DebugLog.snapshot(log.to<JsonObject>(),"");String text=log["text"]|"";root["logText"]=text.substring(text.length()>3000?text.length()-3000:0);}
 if(page=="plots"){JsonDocument samples(storageJsonAllocator());plotSamplesSince(0,0,samples);root["plots"].set(samples);}
 if(page=="playback")PanelRadio::snapshot(root["radio"].to<JsonObject>());
 if(page=="storage-internal"||page=="storage-external"){
  StorageTarget target=page=="storage-external"?StorageTarget::Sd:StorageTarget::Flash;auto status=getStorageSummary(target);root["storage"]["mounted"]=status.mounted;root["storage"]["total"]=status.totalBytes;root["storage"]["free"]=status.freeBytes;
  if(panelStoragePage_!=page){panelStoragePage_=page;panelStoragePath_="/";panelStorageOffset_=0;panelStorageDirty_=true;}
  if(panelStorageMounted_!=status.mounted){panelStorageMounted_=status.mounted;panelStorageDirty_=true;}
  if(panelStorageDirty_){
   JsonDocument listing(storageJsonAllocator());auto files=listing["files"].to<JsonArray>();
   beginStorageRead(target);File dir=storageOpen(target,panelStoragePath_);
   if(!dir||!dir.isDirectory())listing["error"]="Folder unavailable. Insert a card or return to the root folder.";
   else for(int i=0;dir;i++){
    File f=dir.openNextFile();if(!f)break;
    if(i<panelStorageOffset_){f.close();continue;}
    if(files.size()>=12){listing["more"]=true;f.close();break;}
    String name=f.name();name=name.substring(name.lastIndexOf('/')+1);
    auto item=files.add<JsonObject>();item["name"]=name;item["path"]=(panelStoragePath_=="/"?String("/"):panelStoragePath_+"/")+name;item["directory"]=f.isDirectory();item["size"]=f.size();f.close();
   }dir.close();endStorageRead(target);panelStorageListing_="";serializeJson(listing,panelStorageListing_);panelStorageDirty_=false;
  }
  JsonDocument listing(storageJsonAllocator());deserializeJson(listing,panelStorageListing_);
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
  JsonDocument latest(storageJsonAllocator()),patch(storageJsonAllocator());settingsManager_->toJson(settingsGetter_(),latest.to<JsonObject>());
  for(JsonObjectConst change:args["changes"].as<JsonArrayConst>()){
   std::string path=change["path"]|"";size_t split=path.find('/');if(split==std::string::npos){error="Invalid setting";return false;}
   const std::string section=path.substr(0,split);if(patch[section].isNull())patch[section].set(latest[section]);
   if(!PanelSettings::set(patch.as<JsonVariant>(),path,change["value"])){error="Invalid setting";return false;}
  }
  return settingsSaver_(patch,error);
 }
 if(action=="logics"){JsonDocument result;return logicsHandler_&&logicsHandler_(args,result,error);}
 if(action=="bno055")return Bno055::command(args,error);
 if(action=="wifiScan"){if(wifiManager_->startScan())return true;error="Wi-Fi scan could not start; try again";return false;}
 if(action=="radio")return PanelRadio::request(args,error);
 if(action=="formatSd")return requestSdFormat(args["confirmed"]|false,error);
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
