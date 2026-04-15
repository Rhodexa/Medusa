#include "status_led.h"
#include "pins.h"
#include <Adafruit_NeoPixel.h>

#define BLUE_ON_MS      5u
#define BLUE_PERIOD_MS 4000u
#define GREEN_FLASH_MS  50u

static Adafruit_NeoPixel _pixel(1, PIN_WS2812, NEO_GRB + NEO_KHZ800);

void StatusLed::begin() {
    _pixel.begin();
    _pixel.setBrightness(20);
    _set(0, 0, 0);
}

void StatusLed::triggerGreenFlash() {
    _greenPend  = true;
    _greenStart = millis();
}

void StatusLed::setNoNodes(bool noNodes) {
    _noNodes = noNodes;
}

void StatusLed::tick() {
    if (_greenPend && millis() - _greenStart >= GREEN_FLASH_MS)
        _greenPend = false;

    if (_noNodes) {
        // Orange slow pulse while waiting for nodes.
        uint32_t t = millis() % 2000u;
        t < 1000u ? _set(180, 70, 0) : _set(0, 0, 0);
        return;
    }

    uint32_t t  = millis() % BLUE_PERIOD_MS;
    bool blueOn = (t < BLUE_ON_MS);

    if      ( blueOn &&  _greenPend) _set(  0, 160, 130);  // teal
    else if ( blueOn && !_greenPend) _set(  0,   0, 180);  // blue
    else if (!blueOn &&  _greenPend) _set(  0, 180,   0);  // green
    else                             _set(  0,   0,   0);  // off
}

void StatusLed::_set(uint8_t r, uint8_t g, uint8_t b) {
    _pixel.setPixelColor(0, _pixel.Color(r, g, b));
    _pixel.show();
}
