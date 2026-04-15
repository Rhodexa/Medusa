#pragma once
#include <Arduino.h>
#include <WiFiUdp.h>
#include <functional>
#include "medusa_protocol.h"

// A node is considered stale if no hello is received within this window.
// Set to 3x the node's hello interval (3s * 3 = 9s).
#define NODE_STALE_MS  9000

struct NodeInfo {
    uint8_t   mac[6];
    IPAddress ip;
    float     temperature;
    float     humidity;
    uint32_t  lastSeen;   // millis() timestamp of last hello
    bool      active;     // slot is allocated
    bool      online;     // false if stale (no hello within NODE_STALE_MS)
};

class NodeManager {
public:
    void begin();
    void tick();          // call every loop()

    int         nodeCount() const;      // active (online) nodes only
    NodeInfo*   getNode(int index);     // iterate over online nodes
    NodeInfo*   findByMAC(const uint8_t mac[6]);

    // Called by main.cpp to inject the output mask resolver.
    // Signature: uint8_t resolver(const String& mac, const NodeInfo* node)
    using OutputResolver = std::function<uint8_t(const String&, const NodeInfo*)>;
    void setOutputResolver(OutputResolver fn) { _resolver = fn; }

    // Optional callback fired on every received hello (used for status LED / display).
    using HelloCallback = std::function<void()>;
    void setHelloCallback(HelloCallback fn) { _onHello = fn; }

private:
    WiFiUDP        _udp;
    NodeInfo       _nodes[MEDUSA_MAX_NODES];
    OutputResolver _resolver;
    HelloCallback  _onHello;

    void _handleHello(const PktNodeHello& pkt, const IPAddress& senderIP);
    void _sendCommand(const IPAddress& ip, uint8_t outputMask);
    NodeInfo* _allocSlot(const uint8_t mac[6]);

    static String _macStr(const uint8_t mac[6]);
};
