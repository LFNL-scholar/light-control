#pragma once

// GPIO numbers, not physical header positions. Check the actual board first.
#define RF_SCK 12
#define RF_MISO 13
#define RF_MOSI 11
#define RF_CS 10
#define RF_GDO0 4
#define RF_GDO2 5

// Addressable WS2812 on the user's ESP32S3 DevKitC.
#define STATUS_RGB_GPIO 48

#define RF_DEFAULT_MHZ 433.92f
#define RF_DEFAULT_BW_KHZ 325.0f
#define RF_DEFAULT_RATE_KBAUD 4.8f
#define RX_SYMBOL_CAPACITY 4096
#define PULSE_CAPACITY (RX_SYMBOL_CAPACITY * 2)
#define MAX_WAVE_US 3000000U
#define RX_DEFAULT_IDLE_US 20000U
#define RX_DEFAULT_FILTER_US 1U
