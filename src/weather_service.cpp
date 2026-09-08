#include "weather_service.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static void mapWmoCode(int code, String& conditionText, WeatherIconType& iconType) {
    // WMO Weather interpretation codes (WW)
    switch (code) {
        case 0:
            conditionText = "Clear Sky";
            iconType = ICON_SUNNY;
            break;
        case 1:
        case 2:
            conditionText = "Partly Cloudy";
            iconType = ICON_PARTLY_CLOUDY;
            break;
        case 3:
            conditionText = "Overcast";
            iconType = ICON_CLOUDY;
            break;
        case 45:
        case 48:
            conditionText = "Foggy";
            iconType = ICON_FOG;
            break;
        case 51:
        case 53:
        case 55:
            conditionText = "Drizzle";
            iconType = ICON_RAIN;
            break;
        case 61:
        case 63:
        case 65:
            conditionText = "Rain";
            iconType = ICON_RAIN;
            break;
        case 71:
        case 73:
        case 75:
        case 77:
            conditionText = "Snowfall";
            iconType = ICON_SNOW;
            break;
        case 80:
        case 81:
        case 82:
            conditionText = "Rain Showers";
            iconType = ICON_RAIN;
            break;
        case 95:
        case 96:
        case 99:
            conditionText = "Thunderstorm";
            iconType = ICON_THUNDERSTORM;
            break;
        default:
            conditionText = "Fair";
            iconType = ICON_PARTLY_CLOUDY;
            break;
    }
}

WeatherData fetchWeather() {
    WeatherData data;
    data.valid = false;
    data.city = WEATHER_CITY_NAME;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WEATHER] Wi-Fi not connected. Using offline fallback weather values.");
        data.currentTemp = 25.0f;
        data.tempMax = 31.0f;
        data.tempMin = 23.0f;
        data.humidity = 65;
        data.windSpeed = 12.0f;
        data.rainProbability = 20;
        data.conditionText = "Offline / Demo";
        data.iconType = ICON_PARTLY_CLOUDY;
        return data;
    }

    WiFiClientSecure client;
    client.setInsecure(); // No cert verification needed for public API endpoint

    HTTPClient http;
    String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(WEATHER_LATITUDE, 4) +
                 "&longitude=" + String(WEATHER_LONGITUDE, 4) +
                 "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m" +
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max" +
                 "&timezone=auto";

    if (!WEATHER_USE_CELSIUS) {
        url += "&temperature_unit=fahrenheit&wind_speed_unit=mph";
    }

    Serial.println("[WEATHER] Fetching weather from Open-Meteo...");
    http.begin(client, url);
    http.setTimeout(8000);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            data.valid = true;
            data.currentTemp = doc["current"]["temperature_2m"] | 25.0f;
            data.humidity = doc["current"]["relative_humidity_2m"] | 60;
            data.windSpeed = doc["current"]["wind_speed_10m"] | 10.0f;
            data.weatherCode = doc["current"]["weather_code"] | 0;

            data.tempMax = doc["daily"]["temperature_2m_max"][0] | (data.currentTemp + 4.0f);
            data.tempMin = doc["daily"]["temperature_2m_min"][0] | (data.currentTemp - 4.0f);
            data.rainProbability = doc["daily"]["precipitation_probability_max"][0] | 0;

            mapWmoCode(data.weatherCode, data.conditionText, data.iconType);

            Serial.printf("[WEATHER] Success! Temp: %.1f%c, %s, H: %.1f / L: %.1f, Humidity: %d%%, Wind: %.1f\n",
                          data.currentTemp,
                          WEATHER_USE_CELSIUS ? 'C' : 'F',
                          data.conditionText.c_str(),
                          data.tempMax,
                          data.tempMin,
                          data.humidity,
                          data.windSpeed);
        } else {
            Serial.printf("[WEATHER] JSON deserialization failed: %s\n", error.c_str());
        }
    } else {
        Serial.printf("[WEATHER] HTTP GET failed, error code: %d\n", httpCode);
    }

    http.end();
    return data;
}
