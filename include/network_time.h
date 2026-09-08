#pragma once
#include <Arduino.h>
#include "config.h"

struct TimeData {
    bool valid;
    int hour;
    int minute;
    int second;
    int year;
    int month;
    int day;
    int dayOfWeek; // 0 = Sunday, 1 = Monday, etc.
    String timeStr;       // "14:35" or "02:35 PM"
    String dateStr;       // "Wednesday, September 2, 2026"
    String shortDateStr;  // "Sep 2, 2026"
    String dayName;       // "Wednesday"
};

bool connectWiFi();
void disconnectWiFi();
bool syncNTP();
TimeData getFormattedTime();
int getWiFiRSSI();
