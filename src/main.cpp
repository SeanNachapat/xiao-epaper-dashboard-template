#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "pin_config.h"
#include "battery.h"
#include "network_time.h"
#include "weather_service.h"
#include "dashboard_view.h"
#include <esp_sleep.h>

// Global Telemetry & Status (persists in RTC memory across deep sleep)
RTC_DATA_ATTR static int bootCount = 0;
RTC_DATA_ATTR static int activeScreen = 0; // 0 = Page 1 (Daily Brief), 1 = Page 2 (Systems & Portfolio)
static BatteryStatus bat;
static TimeData timeData;
static WeatherData weather;
static int rssi = -60;

static void printBanner() {
    Serial.println("\n=========================================================");
    Serial.println("   TRMNL 7.5\" (OG) DIY KIT - CUSTOM DASHBOARD FIRMWARE   ");
    Serial.println("   Seeed Studio XIAO ESP32-S3 + EE04 ePaper Board        ");
    Serial.println("=========================================================");
    Serial.printf("Boot Count: %d | MCU: ESP32-S3 @ %d MHz\n", bootCount, ESP.getCpuFreqMHz());
    Serial.printf("Flash: %d MB | PSRAM: %d MB | Free Heap: %d KB\n",
                  ESP.getFlashChipSize() / (1024 * 1024),
                  ESP.getPsramSize() / (1024 * 1024),
                  ESP.getFreeHeap() / 1024);
    Serial.println("---------------------------------------------------------");
}

static void enterBoardDeepSleep(uint32_t sleepMinutes) {
    Serial.println("\n[POWER] Entering ultra-low power Deep Sleep...");
    Serial.printf("[POWER] Auto-wake in %u minutes.\n", sleepMinutes);

    // 1. Enable timer wakeup
    uint64_t sleepTimeMicros = static_cast<uint64_t>(sleepMinutes) * 60ULL * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleepTimeMicros);

    // 2. Enable multi-button wakeup (KEY1 on GPIO 2, KEY2 on GPIO 3, KEY3 on GPIO 5)
    uint64_t buttonMask = (1ULL << PIN_KEY1) | (1ULL << PIN_KEY2) | (1ULL << PIN_KEY3);
    esp_sleep_enable_ext1_wakeup(buttonMask, ESP_EXT1_WAKEUP_ANY_LOW);

    Serial.println("[POWER] Going to sleep now. Goodnight!");
    Serial.flush();
    delay(50);
    esp_deep_sleep_start();
}

static void ensureWiFi() {
    if (String(WIFI_SSID) != "YOUR_WIFI_SSID" && WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }
}

static void renderCurrentScreen(int screenIndex) {
    // Only 2 pages exist in the system: 0 (Page 1: Daily Brief) and 1 (Page 2: Systems & Portfolio)
    activeScreen = abs(screenIndex) % 2;

    Serial.println("[DISPLAY] Powering on e-Paper panel (GPIO 43)...");
    displayInit();

    unsigned long renderStart = millis();
    bool streamed = false;

    // Retry up to 3 times to fetch the requested page from GitHub
    for (int attempt = 1; attempt <= 3 && !streamed; attempt++) {
        ensureWiFi();
        if (WiFi.status() == WL_CONNECTED) {
            String base = String(STREAM_BASE_URL);
            while (base.endsWith("/")) base = base.substring(0, base.length() - 1);
            String pathPrefix = (base.indexOf("api/display") != -1 || base.indexOf("github") != -1) ? "" : "/api/display";

            String url = base + pathPrefix + (activeScreen == 0 ? "/page1.bin" : "/page2.bin");
            Serial.printf("[STREAMER] (Attempt %d/3) Streaming Page %d from: %s\n", attempt, activeScreen + 1, url.c_str());
            streamed = fetchAndDisplayLiveStream(url.c_str(), bat, timeData, activeScreen);
            if (streamed) break;
        }
        if (!streamed && attempt < 3) {
            Serial.println("[STREAMER] Reconnecting Wi-Fi and retrying in 1.5s...");
            WiFi.disconnect();
            delay(1500);
            connectWiFi();
        }
    }

    // Clean offline fallback if completely disconnected; never show any unwanted 3rd page!
    if (!streamed) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[DISPLAY] Device is offline. Rendering clean offline screen...");
            renderOfflineScreen();
        } else {
            Serial.println("[STREAMER] GitHub stream unavailable after retries. Preserving current display.");
        }
    }

    Serial.printf("[DISPLAY] Render completed in %lu ms\n", millis() - renderStart);

    Serial.println("[DISPLAY] Putting e-Paper panel into hibernation...");
    displayHibernate();
}

void setup() {
    bootCount++;
    Serial.begin(115200);

    // Give time for USB CDC to connect if plugged into computer
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 1500)) {
        delay(10);
    }

    printBanner();

    // 1. Configure Hardware Buttons (Active-Low)
    pinMode(PIN_KEY1, INPUT_PULLUP);
    pinMode(PIN_KEY2, INPUT_PULLUP);
    pinMode(PIN_KEY3, INPUT_PULLUP);

    // Check Wakeup Cause
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (wakeup_pin_mask & (1ULL << PIN_KEY2)) {
            Serial.println("[WAKEUP] Woken up by KEY 2 (Page 2: Systems & Portfolio)!");
            activeScreen = 1;
        } else if (wakeup_pin_mask & (1ULL << PIN_KEY3)) {
            Serial.println("[WAKEUP] Woken up by KEY 3 (Toggle Page 1 <-> Page 2)!");
            activeScreen = (activeScreen == 0) ? 1 : 0;
        } else {
            Serial.println("[WAKEUP] Woken up by KEY 1 (Page 1: Daily Brief)!");
            activeScreen = 0;
        }
    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("[WAKEUP] Auto-wake by scheduled timer: Cycling to next page...");
        activeScreen = (activeScreen + 1) % 2; // Auto-cycle between Page 1 and Page 2!
    } else {
        Serial.println("[WAKEUP] Power-on / Hardware Reset -> Page 1.");
        activeScreen = 0;
    }

    // 2. Initialize and read battery
    batteryInit();
    bat = readBattery();
    Serial.printf("[BATTERY] Voltage: %.2fV (%d%%, Raw ADC: %d, USB: %s)\n",
                  bat.voltage, bat.percentage, bat.rawAdc, bat.isUsbPowered ? "YES" : "NO");

    // 3. Connect to Wi-Fi (skip if placeholder credentials)
    bool wifiConnected = false;
    if (String(WIFI_SSID) != "YOUR_WIFI_SSID") {
        wifiConnected = connectWiFi();
        if (wifiConnected) {
            rssi = getWiFiRSSI();
            syncNTP();
            timeData = getFormattedTime();
            weather = fetchWeather();
        }
    }

    // 4. Render initial screen (will live-stream from FastAPI if Wi-Fi connected, or use flash fallback)
    renderCurrentScreen(activeScreen);

    // 5. Enter Deep Sleep (if enabled on battery) or stay interactive on USB
    bool isUsb = bat.isUsbPowered || (bool)Serial;
    if (ENABLE_DEEP_SLEEP && !isUsb) {
        if (WiFi.status() == WL_CONNECTED) {
            disconnectWiFi();
        }
        enterBoardDeepSleep(SLEEP_DURATION_MINUTES);
    } else {
        Serial.println("\n[SYSTEM] Running in interactive preview mode:");
        Serial.println("  Press [KEY 1] (Left)   or send '1' -> Page 1: Daily Brief");
        Serial.println("  Press [KEY 2] (Middle) or send '2' -> Page 2: Systems & Portfolio");
        Serial.println("  Press [KEY 3] (Right)  or send '3' -> Toggle Page 1 <-> Page 2");
        Serial.println("  Send '4'                           -> System Diagnostics Screen");
        Serial.println("  Send 's'                           -> Test Deep Sleep");
    }
}

void loop() {
    // 1. Scheduled auto-refresh when active on USB power: cycle to next page
    static unsigned long lastUsbRefresh = millis();
    if (millis() - lastUsbRefresh >= (SLEEP_DURATION_MINUTES * 60000UL)) {
        lastUsbRefresh = millis();
        activeScreen = (activeScreen + 1) % 2; // Auto-cycle between Page 1 and Page 2!
        Serial.printf("\n[TIMER] Scheduled auto-refresh interval reached. Cycling to Page %d...\n", activeScreen + 1);
        ensureWiFi();
        syncNTP();
        timeData = getFormattedTime();
        weather = fetchWeather();
        bat = readBattery();
        renderCurrentScreen(activeScreen);
    }

    // 2. Hardware Button Polling
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 100) {
        lastCheck = millis();

        // KEY 1: Page 1 (Daily Brief)
        if (digitalRead(PIN_KEY1) == LOW) {
            Serial.println("\n[BUTTON] >>> KEY 1 -> Page 1: Daily Brief <<<");
            while (digitalRead(PIN_KEY1) == LOW) delay(10);
            delay(50);
            renderCurrentScreen(0);
        }

        // KEY 2: Page 2 (Systems & Portfolio)
        if (digitalRead(PIN_KEY2) == LOW) {
            Serial.println("\n[BUTTON] >>> KEY 2 -> Page 2: Systems & Portfolio <<<");
            while (digitalRead(PIN_KEY2) == LOW) delay(10);
            delay(50);
            renderCurrentScreen(1);
        }

        // KEY 3: Toggle Page 1 <-> Page 2
        if (digitalRead(PIN_KEY3) == LOW) {
            Serial.println("\n[BUTTON] >>> KEY 3 -> Toggle Page <<<");
            while (digitalRead(PIN_KEY3) == LOW) delay(10);
            delay(50);
            renderCurrentScreen((activeScreen + 1) % 2);
        }
    }

    // Serial Monitor Commands
    if (Serial.available()) {
        char c = Serial.read();
        if (c == '1' || c == 'r' || c == 'R') {
            Serial.println("\n[SERIAL] Key '1' -> Page 1: Daily Brief");
            renderCurrentScreen(0);
        } else if (c == '2') {
            Serial.println("\n[SERIAL] Key '2' -> Page 2: Systems & Portfolio");
            renderCurrentScreen(1);
        } else if (c == '3' || c == 't' || c == 'T') {
            Serial.println("\n[SERIAL] Key '3' -> Toggle Page");
            renderCurrentScreen((activeScreen + 1) % 2);
        } else if (c == '4') {
            Serial.println("\n[SERIAL] Key '4' -> System Diagnostics Screen");
            renderSystemInfoScreen(timeData, weather, bat, rssi);
        } else if (c == 's' || c == 'S') {
            enterBoardDeepSleep(SLEEP_DURATION_MINUTES);
        }
    }

    delay(20);
}
