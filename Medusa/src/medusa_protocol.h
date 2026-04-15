#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// Medusa Mesh Protocol
// Shared between Master and Node firmware — keep in sync.
//
// Transport: UDP unicast
//   Nodes send to 192.168.4.1:MEDUSA_UDP_PORT
//   Master replies to node's IP:MEDUSA_UDP_PORT
// ---------------------------------------------------------------------------

#define MEDUSA_UDP_PORT   4210
#define MEDUSA_MAX_NODES  8
#define MEDUSA_NUM_OUTPUTS 6

// Packet type identifiers
#define PKT_NODE_HELLO  0x01   // Node  → Master : announce + telemetry
#define PKT_MASTER_CMD  0x02   // Master → Node  : output states + current time

// ---------------------------------------------------------------------------
// Packet structs — packed so sizeof() is exact over the wire
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

// Node → Master
// Sent periodically (≤1Hz). Master uses the UDP source IP to track the node.
typedef struct {
    uint8_t  type;          // PKT_NODE_HELLO
    uint8_t  mac[6];        // node's own WiFi STA MAC
    float    temperature;   // °C  (NaN if sensor unavailable)
    float    humidity;      // %RH (NaN if sensor unavailable)
} PktNodeHello;             // 12 bytes

// Master → Node
// Sent in reply to each hello (and proactively if state changed).
typedef struct {
    uint8_t  type;          // PKT_MASTER_CMD
    uint8_t  outputs;       // bitmask: bit N = output N (1=ON, 0=OFF)
    uint32_t unix_time;     // current UTC epoch seconds (node uses for schedule rules)
} PktMasterCmd;             // 6 bytes

#pragma pack(pop)
