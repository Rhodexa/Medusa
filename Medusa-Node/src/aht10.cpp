#include "aht10.h"
#include <Wire.h>

#define AHT10_ADDR      0x38
#define AHT10_CMD_INIT  0xE1
#define AHT10_CMD_MEAS  0xAC
#define AHT10_STATUS_BUSY       (1 << 7)
#define AHT10_STATUS_CALIBRATED (1 << 3)

// Starts Wire on the given pins, waits for sensor power-up, then calibrates.
bool AHT10::begin(uint8_t sda, uint8_t scl) {
    Wire.begin(sda, scl);
    delay(40);   // AHT10 needs 40ms after power-on before first command

    // Check if sensor ACKs its address
    Wire.beginTransmission(AHT10_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[AHT10] Not found on I2C bus");
        return false;
    }

    return _sendInit();
}

// Triggers measurement, waits for ready, parses and returns temp + humidity.
bool AHT10::read(float& temperature, float& humidity) {
    // Trigger measurement
    Wire.beginTransmission(AHT10_ADDR);
    Wire.write(AHT10_CMD_MEAS);
    Wire.write(0x33);
    Wire.write(0x00);
    Wire.endTransmission();

    delay(80);   // measurement takes up to 75ms per datasheet

    if (!_waitReady()) {
        Serial.println("[AHT10] Sensor busy after measurement");
        return false;
    }

    // Read 6 bytes: status + 5 data bytes
    if (Wire.requestFrom((uint8_t)AHT10_ADDR, (uint8_t)6) != 6) {
        Serial.println("[AHT10] Short read");
        return false;
    }

    uint8_t b[6];
    for (int i = 0; i < 6; i++) b[i] = Wire.read();

    // Humidity: bits [39:20] of the 40 data bits (after status byte)
    uint32_t rawHum  = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
    // Temperature: bits [19:0]
    uint32_t rawTemp = (((uint32_t)b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];

    humidity    = (rawHum  / 1048576.0f) * 100.0f;
    temperature = (rawTemp / 1048576.0f) * 200.0f - 50.0f;

    return true;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

// Sends the calibration init command and confirms the calibrated bit is set.
bool AHT10::_sendInit() {
    Wire.beginTransmission(AHT10_ADDR);
    Wire.write(AHT10_CMD_INIT);
    Wire.write(0x08);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(10);

    // Read status byte and confirm calibrated bit
    Wire.requestFrom((uint8_t)AHT10_ADDR, (uint8_t)1);
    if (!Wire.available()) return false;
    uint8_t status = Wire.read();

    _calibrated = (status & AHT10_STATUS_CALIBRATED) != 0;
    if (!_calibrated) Serial.println("[AHT10] Warning: calibration bit not set");

    Serial.println("[AHT10] Ready");
    return true;
}

// Polls the status byte up to 10 times (100ms total) waiting for busy bit to clear.
bool AHT10::_waitReady() {
    for (int i = 0; i < 10; i++) {
        Wire.requestFrom((uint8_t)AHT10_ADDR, (uint8_t)1);
        if (Wire.available() && !(Wire.read() & AHT10_STATUS_BUSY)) return true;
        delay(10);
    }
    return false;
}
