#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// AHT10 — minimal I2C temperature/humidity driver
// Datasheet: ASAIR AHT10, I2C address 0x38
// ---------------------------------------------------------------------------

class AHT10 {
public:
    // Initialise Wire and the sensor. Returns false if sensor not found.
    bool begin(uint8_t sda, uint8_t scl);

    // Triggers a measurement and reads the result. Blocks ~80ms.
    // Returns false if the read fails or sensor reports not-ready.
    bool read(float& temperature, float& humidity);

private:
    bool _calibrated = false;

    bool _sendInit();
    bool _waitReady();
};
