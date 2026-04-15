#include "leds.h"
#include <Adafruit_NeoPixel.h>

// NeoPixel instance — file-scoped, allocated once begin() is called.
static Adafruit_NeoPixel* _strip = nullptr;

// Initialise the NeoPixel strip and turn all LEDs off.
void Leds::begin(uint8_t pin) {
    _strip = new Adafruit_NeoPixel(LED_COUNT, pin, NEO_GRB + NEO_KHZ800);
    _strip->begin();
    _strip->setBrightness(60);
    clear();
}

// Stage a color for one LED. Call show() to push.
void Leds::set(uint8_t index, Color color) {
    if (index >= LED_COUNT) return;
    _buf[index] = color;
}

// Stage the same color for every LED. Call show() to push.
void Leds::setAll(Color color) {
    for (uint8_t i = 0; i < LED_COUNT; i++)
        _buf[i] = color;
}

// Push the staged buffer to the LED chain over the wire.
void Leds::show() {
    if (!_strip) return;
    for (uint8_t i = 0; i < LED_COUNT; i++)
        _strip->setPixelColor(i, _strip->Color(_buf[i].r, _buf[i].g, _buf[i].b));
    _strip->show();
}

// Set all LEDs off and push immediately — no separate show() needed.
void Leds::clear() {
    setAll(Colors::OFF);
    show();
}
