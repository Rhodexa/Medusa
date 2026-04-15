#pragma once

// ---------------------------------------------------------------------------
// Medusa Master — pin assignments (ESP32 WROOM / esp32dev)
// ---------------------------------------------------------------------------

// ST7565P GLCD — hardware SPI (VSPI: SCK=18, MOSI=23, no MISO needed)
#define PIN_DISP_CS    5
#define PIN_DISP_DC   17   // A0 / Register Select
#define PIN_DISP_RST  16

// WS2812 status LED — single data wire
#define PIN_WS2812    13
