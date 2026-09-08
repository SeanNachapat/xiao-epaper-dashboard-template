#pragma once
#include <Arduino.h>

// =============================================================================
// Seeed Studio XIAO ESP32-S3 + EE04 ePaper Display Pin Mapping
// Hardware: Seeed Studio TRMNL 7.5" OG DIY Kit
// Display: 7.5-inch Monochrome 800x480 ePaper (UC8179 / GDEY075T7)
// =============================================================================

// Display SPI & Control Pins
#define PIN_EPD_SCK       7   // D8  - SPI Clock Line
#define PIN_EPD_MOSI      9   // D10 - SPI Master-Out Data Line
#define PIN_EPD_MISO     -1   // Not used by ePaper display
#define PIN_EPD_CS       44   // D7  - Chip Select (Active Low)
#define PIN_EPD_DC       10   // Back Pad - Data/Command Control Pin
#define PIN_EPD_RST      38   // Back Pad - Hardware Reset Pin (Active Low)
#define PIN_EPD_BUSY      4   // D3  - Display Busy Status Pin
#define PIN_EPD_ENABLE   43   // D6  - Display Power Enable Gate (Active HIGH)

// Display Resolution
#define EPD_WIDTH       800
#define EPD_HEIGHT      480

// User Interface Buttons (Active LOW with internal pull-up)
#define PIN_KEY1          2   // D1/A1 - Left Button
#define PIN_KEY2          3   // D2/A2 - Middle Button
#define PIN_KEY3          5   // D4/A4 - Right Button

// Battery Monitoring Circuit
#define PIN_BAT_READ      1   // D0/A0 (GPIO 1) - Resistor Divider Analog Input
#define PIN_BAT_EN        6   // D5/A5 (GPIO 6) - MOSFET Divider Enable (Active HIGH)

// Battery ADC Calculation Constants
// Divider ratio scale factor based on Seeed EE04 schematic: ~7.16V full-scale at 4096 counts
#define BAT_VOLTAGE_SCALE   7.16f
#define BAT_ADC_RESOLUTION  4096.0f
#define BAT_MIN_VOLTAGE     3.20f   // 0% battery threshold
#define BAT_MAX_VOLTAGE     4.20f   // 100% full charge threshold
