#pragma once
#include <Arduino.h>

// Physical strip has 4 LEDs now; set to 6 to support future expansion.
// Extra entries are simply not driven by any hardware.
#define LED_COUNT 6

// ---------------------------------------------------------------------------
// Color type and named constants
// Brightness is intentionally modest — these run as status indicators,
// not room lighting.
// ---------------------------------------------------------------------------

struct Color {
    uint8_t r, g, b;
    constexpr Color(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
    bool operator==(const Color& o) const { return r==o.r && g==o.g && b==o.b; }
};

namespace Colors {
    constexpr Color OFF    = {  0,   0,   0};
    constexpr Color TEAL   = {  0, 160, 130};   // Medusa brand color
    constexpr Color GREEN  = {  0, 180,   0};
    constexpr Color RED    = {180,   0,   0};
    constexpr Color BLUE   = {  0,   0, 180};
    constexpr Color ORANGE = {180,  70,   0};
    constexpr Color WHITE  = {160, 160, 160};
}

// ---------------------------------------------------------------------------
// Leds — minimal WS2812 driver for 4 status LEDs
// ---------------------------------------------------------------------------

class Leds {
public:
    // Initialise hardware. Call once in setup().
    void begin(uint8_t pin);

    // Set a single LED by index (0 = closest to data-in). Does not push yet.
    void set(uint8_t index, Color color);

    // Set all LEDs to the same color. Does not push yet.
    void setAll(Color color);

    // Turn all LEDs off and push immediately.
    void clear();

    // Push the current buffer to the LED chain.
    void show();

private:
    Color _buf[LED_COUNT] = {Colors::OFF, Colors::OFF, Colors::OFF, Colors::OFF, Colors::OFF, Colors::OFF};
};
