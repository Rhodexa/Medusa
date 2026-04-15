#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "pins.h"
#include "leds.h"
#include "medusa_protocol.h"
#include "aht10.h"

// ---------------------------------------------------------------------------
// Config — must match master firmware
// ---------------------------------------------------------------------------

#define MEDUSA_AP_SSID_PREFIX  "Medusa-"
#define MEDUSA_AP_PASS         "wnUjknMdjSUS"
#define MASTER_IP              "192.168.4.1"
#define HELLO_INTERVAL_MS      3000

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static Leds    leds;
static WiFiUDP udp;
static AHT10   aht;
static uint8_t myMAC[6];
static String  masterSSID;   // stored on first connect, reused for reconnects

// ---------------------------------------------------------------------------
// Status LED state
// ---------------------------------------------------------------------------
//
// Physical layout (router case):
//   LED 0        → WiFi light guide — status only
//   LEDs 1-4     → plug symbols    — relay outputs 0-3
//
// LED 0 behaviour:
//   • Blue heartbeat pulse (200 ms on / 2 s period) — running indicator
//   • Green flash (400 ms) on connect or packet received
//   • Teal when both fire simultaneously
//   • Orange on LED 0 during WiFi blocking events (connectToMaster / reconnect)
//
// LEDs 1-4: deep orange when relay ON, off when OFF.

#define NUM_RELAY_OUTPUTS  4
#define RELAY_LED_OFFSET   1    // LEDs 1-4 are the plug symbols

//
// LED 0 behaviour:
//   Blue heartbeat — short 200 ms pulse every 2 s while running.
//   Green flash    — 400 ms on connect or UDP packet received.
//   Teal           — both active simultaneously (rare).
//
#define BLUE_ON_MS     50u   // heartbeat pulse width
#define BLUE_PERIOD_MS 2000u  // heartbeat period
#define GREEN_FLASH_MS 400u   // event flash duration (connect / packet)

static uint8_t  lastMask     = 0;
static bool     greenPending = false;
static uint32_t greenStart   = 0;

// Trigger a brief green flash on LED 0 (connect events, packet received).
static void triggerGreenFlash() { greenPending = true; greenStart = millis(); }

// Called every loop iteration.
static void updateStatusLeds() {
    if (greenPending && millis() - greenStart >= GREEN_FLASH_MS)
        greenPending = false;

    uint32_t t     = millis() % BLUE_PERIOD_MS;
    bool blueOn    = (t < BLUE_ON_MS);

    // Blue heartbeat and green event flash coexist — both on → teal.
    Color statusColor = Colors::OFF;
    if      ( blueOn &&  greenPending) statusColor = Colors::TEAL;
    else if ( blueOn && !greenPending) statusColor = Colors::BLUE;
    else if (!blueOn &&  greenPending) statusColor = Colors::GREEN;

    leds.set(0, statusColor);

    for (int i = 0; i < NUM_RELAY_OUTPUTS; i++)
        leds.set(RELAY_LED_OFFSET + i, (lastMask >> i) & 1 ? Colors::ORANGE : Colors::OFF);

    leds.show();
}

// ---------------------------------------------------------------------------
// Output state machine — round-robin stagger to limit inrush
// ---------------------------------------------------------------------------
//
// targetMask : desired state, updated immediately on command receipt
// currentMask: state actually driven to the pins, catches up one output per tick
// stepIdx    : which output gets reconciled next (cycles 0-5)
//
// stepOutputs() is called at OUTPUT_STEP_HZ from loop(). Each call advances
// stepIdx and, if that output differs from target, drives the pin and updates
// currentMask. Loop never blocks; worst-case lag = MEDUSA_NUM_OUTPUTS ticks.

#define OUTPUT_STEP_HZ  2
#define OUTPUT_STEP_MS  (1000u / OUTPUT_STEP_HZ)

static const uint8_t kOutputPins[MEDUSA_NUM_OUTPUTS] = {
    PIN_OUTPUT_0, PIN_OUTPUT_1, PIN_OUTPUT_2,
    PIN_OUTPUT_3, PIN_OUTPUT_4, PIN_OUTPUT_5
};

static uint8_t targetMask  = 0;
static uint8_t currentMask = 0;
static uint8_t stepIdx     = 0;

static void stepOutputs() {
    uint8_t i = stepIdx;
    stepIdx = (stepIdx + 1) % MEDUSA_NUM_OUTPUTS;

    bool want      = (targetMask  >> i) & 1;
    bool have      = (currentMask >> i) & 1;
    if (want == have) return;

    bool activeLow = (i < NUM_RELAY_OUTPUTS_HW);
    digitalWrite(kOutputPins[i], (activeLow ? !want : want) ? HIGH : LOW);

    if (want) currentMask |=  (1u << i);
    else      currentMask &= ~(1u << i);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Scans for any SSID beginning with "Medusa-" and returns it, or "" if none found.
static String findMasterSSID() {
    Serial.println("[WiFi] Scanning for Medusa network...");
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i).startsWith(MEDUSA_AP_SSID_PREFIX)) {
            Serial.printf("[WiFi] Found: %s\n", WiFi.SSID(i).c_str());
            return WiFi.SSID(i);
        }
    }
    Serial.println("[WiFi] No Medusa network found.");
    return "";
}

// Cleanly disconnects WiFi, then blocks until connected to the given SSID.
// LED 0 blinks teal while connecting (WiFi state = teal), then briefly solid,
// then off. Restarts the UDP socket so it binds to the new interface address.
static void connectToMaster(const String& ssid) {
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(ssid.c_str(), MEDUSA_AP_PASS);
    Serial.printf("[WiFi] Connecting to %s", ssid.c_str());

    bool toggle = false;
    while (WiFi.status() != WL_CONNECTED) {
        leds.set(0, toggle ? Colors::TEAL : Colors::OFF);
        leds.show();
        toggle = !toggle;
        delay(300);
        Serial.print(".");
    }

    Serial.printf("\n[WiFi] Connected — IP: %s  ch=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.channel());

    udp.stop();
    udp.begin(MEDUSA_UDP_PORT);

    leds.set(0, Colors::TEAL);
    leds.show();
    delay(800);
    leds.clear();
}

// Reads the AHT10, builds a PktNodeHello, and sends it to the master.
// Sends NaN for both values if the sensor read fails.
static void sendHello() {
    PktNodeHello pkt;
    pkt.type = PKT_NODE_HELLO;
    memcpy(pkt.mac, myMAC, 6);

    if (!aht.read(pkt.temperature, pkt.humidity)) {
        pkt.temperature = NAN;
        pkt.humidity    = NAN;
    }

    udp.beginPacket(MASTER_IP, MEDUSA_UDP_PORT);
    udp.write((uint8_t*)&pkt, sizeof(pkt));
    udp.endPacket();
}

// Reads one pending UDP packet. Returns true if a valid PktMasterCmd was received.
static bool receiveCommand(PktMasterCmd& out) {
    int size = udp.parsePacket();
    if (size < (int)sizeof(PktMasterCmd)) return false;

    uint8_t buf[sizeof(PktMasterCmd)];
    udp.read(buf, sizeof(buf));
    if (buf[0] != PKT_MASTER_CMD) return false;

    memcpy(&out, buf, sizeof(PktMasterCmd));
    return true;
}

// Stores the desired output mask. Pin changes happen gradually in stepOutputs().
static void applyOutputs(uint8_t mask) { targetMask = mask; }

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    Serial.println("\n[Node] Booting...");

    // Output pins — initialise all off before WiFi starts.
    // Relays (0-3) are active low → idle HIGH; MOSFETs (4-5) active high → idle LOW.
    for (int i = 0; i < MEDUSA_NUM_OUTPUTS; i++) {
        pinMode(kOutputPins[i], OUTPUT);
        digitalWrite(kOutputPins[i], i < NUM_RELAY_OUTPUTS_HW ? HIGH : LOW);
    }

    pinMode(PIN_CFG_BTN, INPUT_PULLUP);

    leds.begin(PIN_WS2812);

    // Brief white flash to confirm the LED chain is wired correctly
    leds.setAll(Colors::WHITE);
    leds.show();
    delay(300);
    leds.clear();

    // Read our own MAC — used as node identity in every hello packet
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);   // maximum transmit power
    esp_read_mac(myMAC, ESP_MAC_WIFI_STA);
    Serial.printf("[Node] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  myMAC[0], myMAC[1], myMAC[2], myMAC[3], myMAC[4], myMAC[5]);

    // Scan until we find a Medusa AP — LED 0 red while searching, off while rescanning
    masterSSID = findMasterSSID();
    while (masterSSID.isEmpty()) {
        leds.set(0, Colors::RED);
        leds.show();
        delay(3000);
        leds.clear();
        masterSSID = findMasterSSID();
    }

    connectToMaster(masterSSID);   // also calls udp.begin() internally
    triggerGreenFlash();
    Serial.printf("[Node] UDP ready on port %d\n", MEDUSA_UDP_PORT);

    if (!aht.begin(PIN_I2C_SDA, PIN_I2C_SCL)) {
        // Non-fatal — node still works, telemetry will send NaN
        leds.set(1, Colors::ORANGE);
        leds.show();
        delay(1000);
        leds.clear();
    }
}

void loop() {
    // If WiFi dropped, show orange and attempt a full clean reconnect.
    // connectToMaster() blocks until connected, so loop() resumes normally after.
    if (WiFi.status() != WL_CONNECTED) {
        leds.set(0, Colors::ORANGE);
        leds.show();
        Serial.println("[Node] Connection lost — reconnecting...");
        // If the master AP changed SSID (e.g. master was swapped), re-scan.
        String found = findMasterSSID();
        if (!found.isEmpty()) masterSSID = found;
        connectToMaster(masterSSID);
        triggerGreenFlash();
        return;
    }

    // Drive output LEDs every iteration (handles heartbeat fade-out too)
    updateStatusLeds();

    // Advance the output state machine — one output per tick, staggered to limit inrush
    static unsigned long lastStep = 0;
    if (millis() - lastStep >= OUTPUT_STEP_MS) {
        lastStep = millis();
        stepOutputs();
    }

    // Send hello to master at HELLO_INTERVAL_MS
    static unsigned long lastHello = 0;
    if (millis() - lastHello >= HELLO_INTERVAL_MS) {
        lastHello = millis();
        sendHello();
    }

    // Check for an incoming command from master
    PktMasterCmd cmd;
    if (receiveCommand(cmd)) {
        Serial.printf("[Node] CMD — outputs: 0x%02X  time: %lu\n",
                      cmd.outputs, (unsigned long)cmd.unix_time);
        applyOutputs(cmd.outputs);
        lastMask = cmd.outputs;
        triggerGreenFlash();
    }
}
