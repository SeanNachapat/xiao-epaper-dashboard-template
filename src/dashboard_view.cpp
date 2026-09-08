#include "dashboard_view.h"
#include <SPI.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <esp_system.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "offline_screen.h"
#include "georgia_clock.h"



// Instantiate display object for GxEPD2
GxEPD2_BW<GxEPD2_750_GDEY075T7, GxEPD2_750_GDEY075T7::HEIGHT> display(
    GxEPD2_750_GDEY075T7(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);

void displayPowerOn() {
    pinMode(PIN_EPD_ENABLE, OUTPUT);
    digitalWrite(PIN_EPD_ENABLE, HIGH);
    delay(50); // Allow boost converter / power rail to stabilize
}

void displayPowerOff() {
    digitalWrite(PIN_EPD_ENABLE, LOW);
}

void displayInit() {
    // 1. Enable hardware power rail for ePaper panel
    displayPowerOn();

    // 2. Configure control pins actively
    pinMode(PIN_EPD_CS, OUTPUT);
    digitalWrite(PIN_EPD_CS, HIGH);

    pinMode(PIN_EPD_DC, OUTPUT);
    digitalWrite(PIN_EPD_DC, HIGH);

    pinMode(PIN_EPD_RST, OUTPUT);
    digitalWrite(PIN_EPD_RST, HIGH);

    pinMode(PIN_EPD_BUSY, INPUT);

    // 3. Initialize hardware SPI
    SPI.begin(PIN_EPD_SCK, PIN_EPD_MISO, PIN_EPD_MOSI, -1);

    // 4. Initialize display (actively drives RST HIGH)
    display.init(115200, false, 20, false);
    display.setRotation(0); // Landscape orientation (800x480)
    display.setTextColor(GxEPD_BLACK);
}

void displayHibernate() {
    display.hibernate();
    displayPowerOff(); // Cut power gate for ultra-low sleep current
}

// -----------------------------------------------------------------------------
// Graphic Icon Renderers
// -----------------------------------------------------------------------------

static void drawBatteryWidget(int x, int y, const BatteryStatus& bat) {
    // Outer battery shell
    display.drawRoundRect(x, y, 42, 20, 3, GxEPD_BLACK);
    display.fillRect(x + 42, y + 5, 3, 10, GxEPD_BLACK); // Terminal cap

    // Inner fill
    int fill = map(constrain(bat.percentage, 0, 100), 0, 100, 0, 36);
    if (fill > 0) {
        display.fillRect(x + 3, y + 3, fill, 14, GxEPD_BLACK);
    }

    // Battery text
    display.setFont(&FreeSans9pt7b);
    display.setCursor(x - 100, y + 15);
    display.print(String(bat.percentage) + "% " + String(bat.voltage, 2) + "V");
}

static void drawWiFiWidget(int x, int y, int rssi) {
    display.setFont(&FreeSans9pt7b);
    display.setCursor(x, y + 15);
    if (WiFi.status() == WL_CONNECTED) {
        display.print("WiFi " + String(rssi) + "dB");
    } else {
        display.print("WiFi: Offline");
    }
}

static void drawWeatherIcon(int x, int y, WeatherIconType type) {
    // Center point (x+30, y+30)
    int cx = x + 30;
    int cy = y + 30;

    switch (type) {
        case ICON_SUNNY:
            display.drawCircle(cx, cy, 14, GxEPD_BLACK);
            display.fillCircle(cx, cy, 12, GxEPD_BLACK);
            // Sun rays
            for (int a = 0; a < 8; a++) {
                float rad = a * (3.14159f / 4.0f);
                int x1 = cx + (int)(cos(rad) * 17);
                int y1 = cy + (int)(sin(rad) * 17);
                int x2 = cx + (int)(cos(rad) * 23);
                int y2 = cy + (int)(sin(rad) * 23);
                display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
            }
            break;

        case ICON_PARTLY_CLOUDY:
            // Sun behind cloud
            display.drawCircle(cx + 8, cy - 6, 10, GxEPD_BLACK);
            display.fillCircle(cx + 8, cy - 6, 9, GxEPD_BLACK);
            // Cloud body
            display.fillCircle(cx - 10, cy + 8, 10, GxEPD_WHITE);
            display.drawCircle(cx - 10, cy + 8, 10, GxEPD_BLACK);
            display.fillCircle(cx + 4, cy + 4, 12, GxEPD_WHITE);
            display.drawCircle(cx + 4, cy + 4, 12, GxEPD_BLACK);
            display.fillRect(cx - 10, cy + 10, 24, 9, GxEPD_WHITE);
            display.drawFastHLine(cx - 10, cy + 18, 24, GxEPD_BLACK);
            break;

        case ICON_CLOUDY:
        case ICON_FOG:
            display.drawCircle(cx - 8, cy + 4, 11, GxEPD_BLACK);
            display.drawCircle(cx + 8, cy, 13, GxEPD_BLACK);
            display.drawCircle(cx + 20, cy + 6, 9, GxEPD_BLACK);
            display.drawFastHLine(cx - 8, cy + 15, 30, GxEPD_BLACK);
            if (type == ICON_FOG) {
                display.drawFastHLine(cx - 15, cy + 20, 45, GxEPD_BLACK);
                display.drawFastHLine(cx - 10, cy + 24, 35, GxEPD_BLACK);
            }
            break;

        case ICON_RAIN:
        case ICON_THUNDERSTORM:
            display.drawCircle(cx - 6, cy - 4, 11, GxEPD_BLACK);
            display.drawCircle(cx + 10, cy - 8, 13, GxEPD_BLACK);
            display.drawFastHLine(cx - 6, cy + 7, 24, GxEPD_BLACK);
            // Rain drops
            display.drawLine(cx - 10, cy + 14, cx - 14, cy + 22, GxEPD_BLACK);
            display.drawLine(cx, cy + 14, cx - 4, cy + 22, GxEPD_BLACK);
            display.drawLine(cx + 10, cy + 14, cx + 6, cy + 22, GxEPD_BLACK);
            break;

        case ICON_SNOW:
            display.drawCircle(cx, cy - 6, 12, GxEPD_BLACK);
            display.drawFastHLine(cx - 12, cy + 6, 24, GxEPD_BLACK);
            display.drawPixel(cx - 8, cy + 15, GxEPD_BLACK);
            display.drawPixel(cx + 2, cy + 15, GxEPD_BLACK);
            display.drawPixel(cx + 12, cy + 15, GxEPD_BLACK);
            break;

        default:
            display.drawCircle(cx, cy, 14, GxEPD_BLACK);
            break;
    }
}

// Fallback definitions if not provided in config.h
#ifndef DASHBOARD_TITLE
#define DASHBOARD_TITLE     "TRMNL DASHBOARD"
#endif
#ifndef DASHBOARD_SUBTITLE
#define DASHBOARD_SUBTITLE  "Personal Information Hub"
#endif
#ifndef CUSTOM_CARD_HEADER
#define CUSTOM_CARD_HEADER  "Today's Focus & Agenda"
#endif
#ifndef CUSTOM_NOTE_1
#define CUSTOM_NOTE_1       "* 09:30 AM  Morning Review & Focus"
#endif
#ifndef CUSTOM_NOTE_2
#define CUSTOM_NOTE_2       "* 02:00 PM  Project Demo & Development"
#endif
#ifndef CUSTOM_NOTE_3
#define CUSTOM_NOTE_3       "* 06:30 PM  Fitness & Relaxation"
#endif
#ifndef CUSTOM_MOTTO
#define CUSTOM_MOTTO        "\"Small daily improvements lead to stunning results.\""
#endif

// -----------------------------------------------------------------------------
// Main Dashboard Screen
// -----------------------------------------------------------------------------

void renderDashboard(const TimeData& timeData, const WeatherData& weather, const BatteryStatus& battery, int wifiRssi, const String& statusMsg) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // =========================================================================
        // 1. TOP HEADER BAR
        // =========================================================================
        // Day and Date (Left)
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(25, 38);
        display.print(timeData.dateStr.length() > 0 ? timeData.dateStr : "Wednesday, September 2, 2026");

        // Wi-Fi & Battery Status (Right)
        drawWiFiWidget(540, 20, wifiRssi);
        drawBatteryWidget(730, 20, battery);

        // Header double divider line
        display.drawFastHLine(20, 52, 760, GxEPD_BLACK);
        display.drawFastHLine(20, 54, 760, GxEPD_BLACK);

        // =========================================================================
        // 2. HERO DIGITAL CLOCK & GREETING BAR
        // =========================================================================
        // Big Time Display
        display.setFont(&FreeSansBold24pt7b);
        display.setCursor(25, 115);
        display.print(timeData.timeStr);

        // Subtitle / Location
        display.setFont(&FreeSans9pt7b);
        display.setCursor(170, 95);
        display.print(String(DASHBOARD_TITLE) + " • " + weather.city);
        display.setCursor(170, 118);
        display.print(DASHBOARD_SUBTITLE);

        // Subtle divider
        display.drawFastHLine(20, 138, 760, GxEPD_BLACK);

        // =========================================================================
        // 3. LEFT COLUMN: WEATHER CARD
        // =========================================================================
        int c1_x = 20, c1_y = 150, c1_w = 365, c1_h = 265;
        display.drawRoundRect(c1_x, c1_y, c1_w, c1_h, 8, GxEPD_BLACK);
        display.fillRoundRect(c1_x, c1_y, c1_w, 30, 8, GxEPD_BLACK);
        display.fillRect(c1_x, c1_y + 18, c1_w, 12, GxEPD_BLACK);

        // Card Header (White on Black)
        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(c1_x + 12, c1_y + 20);
        display.print("LIVE WEATHER & FORECAST");
        display.setTextColor(GxEPD_BLACK);

        // Weather Icon & Temp
        drawWeatherIcon(c1_x + 15, c1_y + 45, weather.iconType);

        display.setFont(&FreeSansBold24pt7b);
        display.setCursor(c1_x + 90, c1_y + 85);
        display.print(String((int)round(weather.currentTemp)) + (WEATHER_USE_CELSIUS ? " C" : " F"));
        // Degree symbol
        display.drawCircle(c1_x + 90 + ((int)round(weather.currentTemp) >= 10 ? 60 : 45), c1_y + 58, 4, GxEPD_BLACK);

        // Weather Condition Text
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(c1_x + 20, c1_y + 130);
        display.print(weather.conditionText);

        display.drawFastHLine(c1_x + 15, c1_y + 142, c1_w - 30, GxEPD_BLACK);

        // Detailed Metrics
        display.setFont(&FreeSans9pt7b);
        int mY = c1_y + 168;
        int mStep = 24;

        display.setCursor(c1_x + 20, mY);
        display.print("Daily Range : H " + String((int)weather.tempMax) + "° / L " + String((int)weather.tempMin) + "°");
        mY += mStep;

        display.setCursor(c1_x + 20, mY);
        display.print("Humidity    : " + String(weather.humidity) + "%");
        mY += mStep;

        display.setCursor(c1_x + 20, mY);
        display.print("Wind Speed  : " + String(weather.windSpeed, 1) + (WEATHER_USE_CELSIUS ? " km/h" : " mph"));
        mY += mStep;

        display.setCursor(c1_x + 20, mY);
        display.print("Precipitation: " + String(weather.rainProbability) + "% chance");

        // =========================================================================
        // 4. RIGHT COLUMN: CUSTOM AGENDA & FOCUS CARD
        // =========================================================================
        int c2_x = 405, c2_y = 150, c2_w = 375, c2_h = 265;
        display.drawRoundRect(c2_x, c2_y, c2_w, c2_h, 8, GxEPD_BLACK);
        display.fillRoundRect(c2_x, c2_y, c2_w, 30, 8, GxEPD_BLACK);
        display.fillRect(c2_x, c2_y + 18, c2_w, 12, GxEPD_BLACK);

        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(c2_x + 12, c2_y + 20);
        display.print(CUSTOM_CARD_HEADER);
        display.setTextColor(GxEPD_BLACK);

        // Agenda Items
        display.setFont(&FreeSans9pt7b);
        int aY = c2_y + 60;
        int aStep = 30;

        display.setCursor(c2_x + 15, aY);
        display.print(CUSTOM_NOTE_1);
        aY += aStep;

        display.setCursor(c2_x + 15, aY);
        display.print(CUSTOM_NOTE_2);
        aY += aStep;

        display.setCursor(c2_x + 15, aY);
        display.print(CUSTOM_NOTE_3);
        aY += aStep;

        // Inverted Motto / Quote Footer Banner inside Card
        int qY = c2_y + 160;
        display.fillRoundRect(c2_x + 12, qY, c2_w - 24, 90, 6, GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeSans9pt7b);
        display.setCursor(c2_x + 22, qY + 30);
        display.print("Daily Inspiration:");
        display.setCursor(c2_x + 22, qY + 55);
        display.print(CUSTOM_MOTTO);
        display.setTextColor(GxEPD_BLACK);

        // =========================================================================
        // 5. BOTTOM FOOTER BAR
        // =========================================================================
        display.drawFastHLine(20, 430, 760, GxEPD_BLACK);

        display.setFont(&FreeSans9pt7b);
        display.setCursor(20, 452);
        if (statusMsg.length() > 0) {
            display.print(statusMsg);
        } else {
            display.print("Auto-refresh every " + String(SLEEP_DURATION_MINUTES) + "m • [KEY1: Refresh] • [KEY2: System Specs]");
        }

        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(20, 472);
        display.print("TRMNL 7.5\" (OG) DIY Kit • Seeed Studio EE04 + XIAO ESP32-S3");

    } while (display.nextPage());
}

// -----------------------------------------------------------------------------
// System Info & Network Diagnostics Screen (KEY2)
// -----------------------------------------------------------------------------

void renderSystemInfoScreen(const TimeData& timeData, const WeatherData& weather, const BatteryStatus& battery, int wifiRssi) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // Header
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(25, 38);
        display.print("SYSTEM & NETWORK DIAGNOSTICS");

        drawBatteryWidget(730, 20, battery);

        display.drawFastHLine(20, 52, 760, GxEPD_BLACK);
        display.drawFastHLine(20, 54, 760, GxEPD_BLACK);

        // Left Card: MCU & Hardware
        int c1_x = 20, c1_y = 75, c1_w = 365, c1_h = 345;
        display.drawRoundRect(c1_x, c1_y, c1_w, c1_h, 8, GxEPD_BLACK);
        display.fillRoundRect(c1_x, c1_y, c1_w, 30, 8, GxEPD_BLACK);
        display.fillRect(c1_x, c1_y + 18, c1_w, 12, GxEPD_BLACK);

        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(c1_x + 12, c1_y + 20);
        display.print("HARDWARE & FIRMWARE");
        display.setTextColor(GxEPD_BLACK);

        display.setFont(&FreeSans9pt7b);
        int lY = c1_y + 55;
        int step = 26;

        display.setCursor(c1_x + 15, lY); display.print("MCU: ESP32-S3 (Xtensa Dual 240MHz)"); lY += step;
        display.setCursor(c1_x + 15, lY); display.print("Flash: " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB QIO"); lY += step;
        display.setCursor(c1_x + 15, lY); display.print("PSRAM: " + String(ESP.getPsramSize() / (1024 * 1024)) + " MB (Free: " + String(ESP.getFreePsram() / 1024) + " KB)"); lY += step;
        display.setCursor(c1_x + 15, lY); display.print("Heap Free: " + String(ESP.getFreeHeap() / 1024) + " KB"); lY += step;
        display.setCursor(c1_x + 15, lY); display.print("Display: 7.5\" 800x480 (UC8179)"); lY += step;
        display.setCursor(c1_x + 15, lY); display.print("Power Gate: GPIO 43 (PIN_EPD_ENABLE)"); lY += step;
        display.setCursor(c1_x + 15, lY); display.print("Battery: " + String(battery.voltage, 2) + "V (" + String(battery.percentage) + "%)"); lY += step;
        display.setCursor(c1_x + 15, lY); display.print("Sleep Profile: " + String(SLEEP_DURATION_MINUTES) + " minutes interval"); lY += step;
        display.setCursor(c1_x + 15, lY); display.print("Uptime: " + String(millis() / 1000) + " seconds");

        // Right Card: Wi-Fi & Services
        int c2_x = 405, c2_y = 75, c2_w = 375, c2_h = 345;
        display.drawRoundRect(c2_x, c2_y, c2_w, c2_h, 8, GxEPD_BLACK);
        display.fillRoundRect(c2_x, c2_y, c2_w, 30, 8, GxEPD_BLACK);
        display.fillRect(c2_x, c2_y + 18, c2_w, 12, GxEPD_BLACK);

        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(c2_x + 12, c2_y + 20);
        display.print("NETWORK & API STATUS");
        display.setTextColor(GxEPD_BLACK);

        display.setFont(&FreeSans9pt7b);
        lY = c2_y + 55;

        display.setCursor(c2_x + 15, lY); display.print("SSID: " + String(WIFI_SSID)); lY += step;
        display.setCursor(c2_x + 15, lY); display.print("Status: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected")); lY += step;
        display.setCursor(c2_x + 15, lY); display.print("IP Address: " + WiFi.localIP().toString()); lY += step;
        display.setCursor(c2_x + 15, lY); display.print("Signal Strength: " + String(wifiRssi) + " dBm"); lY += step;
        display.setCursor(c2_x + 15, lY); display.print("NTP Sync: " + String(timeData.valid ? "Synchronized" : "Pending/Offline")); lY += step;
        display.setCursor(c2_x + 15, lY); display.print("Weather API: " + String(weather.valid ? "Active (Open-Meteo)" : "Offline")); lY += step;
        display.setCursor(c2_x + 15, lY); display.print("City: " + weather.city); lY += step;
        display.setCursor(c2_x + 15, lY); display.print("Coordinates: " + String(WEATHER_LATITUDE, 2) + ", " + String(WEATHER_LONGITUDE, 2));

        // Footer
        display.drawFastHLine(20, 435, 760, GxEPD_BLACK);
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(20, 460);
        display.print("Press [KEY1] to return to Main Dashboard.");

    } while (display.nextPage());
}

// -----------------------------------------------------------------------------
// Render Clean Minimal Offline Screen (Centered Offline Symbol & Text)
// -----------------------------------------------------------------------------
void renderOfflineScreen() {
    Serial.println("[DISPLAY] Rendering Pixel-Perfect Clean Offline Screen...");
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawBitmap(0, 0, epd_screen_offline, 800, 480, GxEPD_BLACK);
    } while (display.nextPage());
    Serial.println("[DISPLAY] Offline screen render completed.");
}


// -----------------------------------------------------------------------------
// Live Wi-Fi Streamer: Fetches 48,000 bytes raw GxEPD2 buffer from FastAPI server
// -----------------------------------------------------------------------------
bool fetchAndDisplayLiveStream(const char* url, const BatteryStatus& battery, const TimeData& timeData, int screenIndex) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[STREAMER] Wi-Fi not connected, skipping live stream.");
        return false;
    }

    Serial.printf("[STREAMER] Requesting live stream from %s ...\n", url);
    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    WiFiClientSecure secureClient;
    WiFiClient standardClient;
    bool isHttps = (strncmp(url, "https://", 8) == 0);

    bool beginSuccess = false;
    if (isHttps) {
        secureClient.setInsecure(); // Skip certificate verification for GitHub / Cloud CDN
        beginSuccess = http.begin(secureClient, url);
    } else {
        beginSuccess = http.begin(standardClient, url);
    }

    if (!beginSuccess) {
        Serial.println("[STREAMER] Error: http.begin failed.");
        return false;
    }

    #ifdef GITHUB_TOKEN
    if (strlen(GITHUB_TOKEN) > 0) {
        Serial.println("[STREAMER] Attaching GitHub Token to request...");
        http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
    }
    #endif

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[STREAMER] HTTP failed, code: %d\n", httpCode);
        http.end();
        return false;
    }

    uint8_t* streamBuffer = nullptr;
    if (psramFound()) {
        streamBuffer = (uint8_t*)ps_malloc(48000);
    }
    if (!streamBuffer) {
        streamBuffer = (uint8_t*)malloc(48000);
    }
    if (!streamBuffer) {
        Serial.println("[STREAMER] Error: Failed to allocate 48KB buffer!");
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t bytesRead = 0;
    unsigned long start = millis();
    while (http.connected() && bytesRead < 48000 && (millis() - start < 15000)) {
        size_t avail = stream->available();
        if (avail > 0) {
            size_t toRead = min(avail, 48000 - bytesRead);
            stream->readBytes(streamBuffer + bytesRead, toRead);
            bytesRead += toRead;
        } else {
            delay(5);
        }
    }
    http.end();

    if (bytesRead < 48000) {
        Serial.printf("[STREAMER] Incomplete payload: %u / 48000 bytes received.\n", bytesRead);
        free(streamBuffer);
        return false;
    }

    Serial.println("[STREAMER] 48,000 bytes received. Blitting with live Edge Hardware overlays...");
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.drawBitmap(0, 0, streamBuffer, 800, 480, GxEPD_BLACK);

        // =====================================================================
        // HARDWARE OVERLAY: Physical Battery ADC (No 'Bat' text, icon & % only)
        // =====================================================================
        // Clear mock cloud battery area (X: 720..785, Y: 12..36)
        display.fillRect(720, 12, 65, 26, GxEPD_WHITE);

        // Draw live hardware battery percentage
        display.setFont(&FreeSansBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        String batText = String(battery.percentage) + "%";
        display.setCursor(724, 29);
        display.print(batText);

        // Draw live physical battery icon
        int bx = 766, by = 16;
        display.fillRect(bx + 3, by - 2, 5, 2, GxEPD_BLACK); // Top nipple
        display.drawRect(bx, by, 11, 18, GxEPD_BLACK);       // Outer shell
        int fillH = map(constrain(battery.percentage, 0, 100), 0, 100, 0, 14);
        if (fillH > 0) {
            display.fillRect(bx + 2, by + 16 - fillH, 7, fillH, GxEPD_BLACK);
        }
        if (battery.isUsbPowered) {
            // Charging bolt
            display.drawLine(bx + 5, by + 3, bx + 3, by + 9, GxEPD_BLACK);
            display.drawLine(bx + 3, by + 9, bx + 7, by + 9, GxEPD_BLACK);
            display.drawLine(bx + 7, by + 9, bx + 5, by + 14, GxEPD_BLACK);
        }

        // =====================================================================
        // HARDWARE OVERLAY: Real-Time Georgia Bold Clock (Page 1 Only)
        // =====================================================================
        if (screenIndex == 0 && timeData.valid) {
            drawGeorgiaHeroClock(display, 16, 78, timeData.hour, timeData.minute);
        }

        // =====================================================================
        // HARDWARE OVERLAY: Live Edge Footer Refresh Timestamp (Pages 1 & 2)
        // =====================================================================
        if (timeData.valid) {
            display.fillRect(30, 448, 300, 24, GxEPD_WHITE);
            display.setFont(&FreeSansBold9pt7b);
            display.setTextColor(GxEPD_BLACK);
            int h12 = timeData.hour % 12;
            if (h12 == 0) h12 = 12;
            char refBuf[48];
            snprintf(refBuf, sizeof(refBuf), "REFRESH: %d:%02d %s ICT | NEXT CYCLE IN 30M",
                     h12, timeData.minute, (timeData.hour >= 12 ? "PM" : "AM"));
            display.setCursor(32, 465);
            display.print(refBuf);
        }

    } while (display.nextPage());

    free(streamBuffer);
    Serial.println("[STREAMER] Live display blit with Edge overlays complete!");
    return true;
}
