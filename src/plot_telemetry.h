#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
void publishPlotSample(const char* plot,const char* series,const char* unit,uint32_t time,double value);
void plotSamplesSince(uint32_t after,uint32_t boot,JsonDocument& response);
void plotSerialNext(uint32_t after,uint32_t boot,JsonDocument& response);
