#include "web_server.h"
#if APP_HAS_ONBOARD_PANEL && !defined(APP_DISABLE_WEB_UI)
#include "panel_settings.h"
#include "storage_memory.h"
#include "system_metrics.h"
#include "device_log.h"
#include "plot_telemetry.h"
#include "version.h"
namespace {
bool selected(JsonVariantConst values,const char* fragment=nullptr){
 if(values.is<const char*>()){String s=values.as<String>();return s.length()&&s!="none"&&(!fragment||s.indexOf(fragment)>=0);}
 for(JsonVariantConst v:values.as<JsonArrayConst>())if(selected(v,fragment))return true;return false;
}
}
void WebServerManager::panelSnapshot(const String& page,JsonObject root){
 root["version"]=APP_VERSION;security_.status(root["security"].to<JsonObject>());
 if(security_.locked())return;
 SettingsBundle settings=settingsGetter_();settingsManager_->toJson(settings,root["settings"].to<JsonObject>());
 auto config=root["settings"];config["wifi"].remove("password");config["wifi"].remove("apPassword");config["mqtt"].remove("password");config["webAuth"].remove("password");
 JsonVariantConst profiles=config["ui"]["peripheralProfiles"];
 auto caps=root["caps"].to<JsonObject>();
 caps["wled"]=selected(profiles["controls"],"ws2812");caps["motor"]=selected(profiles["controls"],"motor-driver");
 caps["playback"]=selected(profiles["audioProfiles"])||selected(profiles["audioProfile"]);caps["effects"]=caps["playback"];
 caps["battery"]=selected(profiles["sensors"],"voltage-divider")&&settings.battery.adcPin>0;
 caps["oled"]=selected(profiles["displayProfiles"])||selected(profiles["displayProfile"])||settings.oled.enabled;
 caps["storage-external"]=selected(profiles["storage"]);caps["migration"]=false;
 static bool hasPlots=false;static uint32_t graphAt=0;bool detailed=page=="logics"||page=="plots"||millis()-graphAt>5000;
 JsonDocument graph(storageJsonAllocator());if(logicsGetter_)logicsGetter_(graph,detailed);
 root["logics"].set(graph);
 // Runtime snapshot carries plot definitions even when the editor graph is omitted.
 if(detailed&&!graph["graph"].isNull()){graphAt=millis();hasPlots=false;for(JsonObjectConst n:graph["graph"]["nodes"].as<JsonArrayConst>())if(n["type"]=="mainboard.plot")hasPlots=true;}caps["plots"]=hasPlots;
 if(page=="wled"&&ledStatusAppender_)ledStatusAppender_(root);
 if(page=="motor"&&motorStatusAppender_)motorStatusAppender_(root);
 if(page=="hardware"||page=="info"||page=="gpio")appendSystemMetricsJson(root);
 if(page=="firmware")otaManager_->appendStatusJson(root);
 if(page=="logs"){JsonDocument log(storageJsonAllocator());DebugLog.snapshot(log.to<JsonObject>(),"");String text=log["text"]|"";root["logText"]=text.substring(text.length()>3000?text.length()-3000:0);}
 if(page=="plots"){JsonDocument samples(storageJsonAllocator());plotSamplesSince(0,0,samples);root["plots"].set(samples);}
 if(page=="storage-internal"||page=="storage-external"){
  StorageTarget target=page=="storage-external"?StorageTarget::Sd:StorageTarget::Flash;auto status=getStorageSummary(target);root["storage"]["mounted"]=status.mounted;root["storage"]["total"]=status.totalBytes;root["storage"]["free"]=status.freeBytes;
  auto files=root["files"].to<JsonArray>();File dir=storageOpen(target,"/");for(int i=0;dir&&i<32;i++){File f=dir.openNextFile();if(!f)break;auto item=files.add<JsonObject>();item["name"]=f.name();item["directory"]=f.isDirectory();item["size"]=f.size();f.close();}dir.close();
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
 if(action=="volume"){if(volumeHandler_)volumeHandler_(constrain(args["value"]|0,0,100));return true;}
 if(action=="stop"){if(stopHandler_)stopHandler_();return true;}
 if(action=="play")return playHandler_&&playHandler_(args["url"]|"","","media",error);
 if(action=="seek")return seekHandler_&&seekHandler_(args["value"]|0);
 if(action=="mqtt")return mqttHandler_&&mqttHandler_(args["action"]|"",error);
 if(action=="motor")return motorRunHandler_&&motorRunHandler_(args["channel"]|0,args["forward"]|true,args["durationMs"]|0,args["limitInputIndex"]|-1,error);
 if(action=="otaCheck")return otaHandler_&&otaHandler_(false);
 if(action=="otaInstall")return otaManager_->triggerInstallSelected(error);
 if(action=="reboot"){if(rebootHandler_)rebootHandler_();return true;}
 error="Control is unavailable";return false;
}
#endif
