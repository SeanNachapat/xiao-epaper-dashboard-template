#include "network_time.h"
#include <WiFi.h>
#include <time.h>
#include <esp_sntp.h>

static const char* DAY_NAMES[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char* MONTH_NAMES[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char* MONTH_FULL[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

bool connectWiFi() {
    if (String(WIFI_SSID) == "YOUR_WIFI_SSID" || strlen(WIFI_SSID) == 0) {
        Serial.println("[WIFI] Default placeholder SSID detected. Skipping Wi-Fi connection.");
        return false;
    }

    Serial.printf("[WIFI] Connecting to SSID: %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
        delay(300);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI] Connected! IP: %s (RSSI: %d dBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    } else {
        Serial.println("[WIFI] Connection timed out / failed.");
        return false;
    }
}

void disconnectWiFi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[WIFI] Radio powered off to save battery.");
}

bool syncNTP() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
    
    struct tm timeinfo;
    unsigned long startWait = millis();
    while (!getLocalTime(&timeinfo) && (millis() - startWait < 5000)) {
        delay(200);
    }

    if (getLocalTime(&timeinfo)) {
        Serial.printf("[NTP] Time synchronized: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    } else {
        Serial.println("[NTP] Failed to obtain time from NTP servers.");
        return false;
    }
}

TimeData getFormattedTime() {
    TimeData td;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        td.valid = false;
        td.hour = 12;
        td.minute = 0;
        td.second = 0;
        td.year = 2026;
        td.month = 9;
        td.day = 2;
        td.dayOfWeek = 3;
        td.timeStr = "--:--";
        td.dateStr = "Date Not Synchronized";
        td.shortDateStr = "No NTP";
        td.dayName = "TRMNL";
        return td;
    }

    td.valid = true;
    td.hour = timeinfo.tm_hour;
    td.minute = timeinfo.tm_min;
    td.second = timeinfo.tm_sec;
    td.year = timeinfo.tm_year + 1900;
    td.month = timeinfo.tm_mon + 1;
    td.day = timeinfo.tm_mday;
    td.dayOfWeek = timeinfo.tm_wday;

    char timeBuf[10];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", td.hour, td.minute);
    td.timeStr = String(timeBuf);

    char dateBuf[64];
    snprintf(dateBuf, sizeof(dateBuf), "%s, %s %d, %04d",
             DAY_NAMES[td.dayOfWeek % 7],
             MONTH_FULL[(td.month - 1) % 12],
             td.day,
             td.year);
    td.dateStr = String(dateBuf);

    char shortBuf[32];
    snprintf(shortBuf, sizeof(shortBuf), "%s %d, %04d",
             MONTH_NAMES[(td.month - 1) % 12],
             td.day,
             td.year);
    td.shortDateStr = String(shortBuf);
    td.dayName = String(DAY_NAMES[td.dayOfWeek % 7]);

    return td;
}

int getWiFiRSSI() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}
