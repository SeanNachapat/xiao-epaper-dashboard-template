#pragma once
#include <Arduino.h>
#include "pin_config.h"

struct BatteryStatus {
    float voltage;
    int percentage;
    int rawAdc;
    bool isUsbPowered;
};

void batteryInit();
BatteryStatus readBattery();
