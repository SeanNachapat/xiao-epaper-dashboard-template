#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_750_GDEY075T7.h>
#include "pin_config.h"
#include "battery.h"
#include "network_time.h"
#include "weather_service.h"

extern GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT> display;

void displayPowerOn();
void displayPowerOff();
void displayInit();
void displayHibernate();

void renderDashboard(const TimeData& timeData, const WeatherData& weather, const BatteryStatus& battery, int wifiRssi, const String& statusMsg = "");
void renderSystemInfoScreen(const TimeData& timeData, const WeatherData& weather, const BatteryStatus& battery, int wifiRssi);
void renderOfflineScreen();
bool fetchAndDisplayLiveStream(const char* url, const BatteryStatus& battery, const TimeData& timeData, int screenIndex = 0);

