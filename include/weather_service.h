#pragma once
#include <Arduino.h>
#include "config.h"

enum WeatherIconType {
    ICON_SUNNY = 0,
    ICON_PARTLY_CLOUDY,
    ICON_CLOUDY,
    ICON_FOG,
    ICON_RAIN,
    ICON_THUNDERSTORM,
    ICON_SNOW,
    ICON_UNKNOWN
};

struct WeatherData {
    bool valid;
    float currentTemp;
    float tempMax;
    float tempMin;
    int humidity;
    float windSpeed;
    int rainProbability;
    int weatherCode;
    String conditionText;
    WeatherIconType iconType;
    String city;
};

WeatherData fetchWeather();
