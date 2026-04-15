#pragma once
#include <Arduino.h>
#include "node_manager.h"
#include "config_manager.h"
#include "wifi_manager.h"

// ---------------------------------------------------------------------------
// Display — ST7565P 128×64 GLCD, custom driver, hardware SPI (VSPI: SCK=18, MOSI=23).
//
// Pages rotate every PAGE_DURATION_MS:
//   Page 0        — master status (AP SSID, home WiFi, node count)
//   Pages 1..N    — one page per online node (label, temp, humidity)
// ---------------------------------------------------------------------------

class Display {
public:
    void begin();
    void tick();   // call every loop()

    void setManagers(NodeManager* nodes, ConfigManager* cfg, WiFiManager* wifi);

private:
    NodeManager*  _nodes = nullptr;
    ConfigManager* _cfg  = nullptr;
    WiFiManager*  _wifi  = nullptr;

    bool          _ready    = false;
    unsigned long _lastDraw = 0;

    void _draw();
};
