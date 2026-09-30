#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string>
void beginPlotStorage();
bool queuePlotRecording(JsonVariantConst args,std::string& error);
bool plotRecordingPath(const char* folder,const char* plot,String& path);
void plotRecordingStatus(JsonObject status);
bool readPlotHistory(const String& path,uint32_t offset,double from,double to,JsonDocument& response,String& error);
