#pragma once

// ---------------------------------------------------------------------------
// Medusa Node — ESP32-C3 Super Mini pin definitions
//
// Outputs 0-3: relay module, ACTIVE LOW  (HIGH = off, LOW = on)
// Outputs 4-5: MOSFET,       active HIGH (LOW  = off, HIGH = on)
// GPIO 2 is left as spare — it's a strapping pin and driving it LOW at boot
// would cause a relay click before firmware has control.
// ---------------------------------------------------------------------------

// Outputs 0-3: relay (active LOW)
#define PIN_OUTPUT_0   0
#define PIN_OUTPUT_1   1
#define PIN_OUTPUT_2   3
#define PIN_OUTPUT_3   4
// Outputs 4-5: MOSFET 12V (active HIGH)
#define PIN_OUTPUT_4   5
#define PIN_OUTPUT_5   6

#define NUM_RELAY_OUTPUTS_HW  4   // outputs 0-3 are active-low relays

// WS2812 status LED chain (4 LEDs)
#define PIN_WS2812     7

// I2C — AHT10 temp/humidity sensor
// GPIO 8 has an internal pull-up, which suits I2C SDA.
#define PIN_I2C_SDA    8
#define PIN_I2C_SCL    10

// Onboard BOOT button — repurposed as config trigger (active LOW, INPUT_PULLUP)
#define PIN_CFG_BTN    9

// Spare: GPIO 2
