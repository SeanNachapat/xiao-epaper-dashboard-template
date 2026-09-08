#include "battery.h"

void batteryInit() {
    pinMode(PIN_BAT_EN, OUTPUT);
    digitalWrite(PIN_BAT_EN, LOW); // Keep off by default to conserve power
    pinMode(PIN_BAT_READ, INPUT);
    analogReadResolution(12);      // 12-bit ADC (0-4095)
}

BatteryStatus readBattery() {
    BatteryStatus status;

    // 1. Enable the voltage divider MOSFET switch
    digitalWrite(PIN_BAT_EN, HIGH);
    delay(15); // Wait for capacitor / divider voltage stabilization

    // 2. Perform multiple samples for noise reduction
    const int SAMPLES = 8;
    int adcSum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        adcSum += analogRead(PIN_BAT_READ);
        delayMicroseconds(200);
    }

    // 3. Disable the divider immediately to save power
    digitalWrite(PIN_BAT_EN, LOW);

    status.rawAdc = adcSum / SAMPLES;

    // 4. Calculate real voltage
    status.voltage = (static_cast<float>(status.rawAdc) / BAT_ADC_RESOLUTION) * BAT_VOLTAGE_SCALE;

    // 5. Calculate percentage (LiPo typical discharge curve approximation)
    if (status.voltage >= BAT_MAX_VOLTAGE) {
        status.percentage = 100;
    } else if (status.voltage <= BAT_MIN_VOLTAGE) {
        status.percentage = 0;
    } else {
        status.percentage = static_cast<int>(((status.voltage - BAT_MIN_VOLTAGE) / (BAT_MAX_VOLTAGE - BAT_MIN_VOLTAGE)) * 100.0f);
    }

    // If voltage is < 2.0V, no battery is connected (running purely on USB 5V)
    // If voltage is >= 4.20V, battery is actively charging via USB
    status.isUsbPowered = (status.voltage < 2.0f || status.voltage >= 4.20f);

    return status;
}
