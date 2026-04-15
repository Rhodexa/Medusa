#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// StatusLed — RGB indicator for the master unit.
//
//   Blue heartbeat pulse (200 ms on / 2 s period)   — system running
//   Green flash (400 ms)                             — node hello received
//   Teal                                             — both simultaneously
//   Orange slow pulse (1 s on / 2 s period)          — no nodes online
// ---------------------------------------------------------------------------

class StatusLed {
public:
    void begin();
    void tick();

    void triggerGreenFlash();        // call on each node hello
    void setNoNodes(bool noNodes);   // switches between normal / idle state

private:
    bool     _noNodes    = true;
    bool     _greenPend  = false;
    uint32_t _greenStart = 0;

    void _set(uint8_t r, uint8_t g, uint8_t b);
};
