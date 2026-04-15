#include "node_manager.h"
#include <WiFi.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

// Clears the node table and starts listening for UDP packets on MEDUSA_UDP_PORT.
void NodeManager::begin() {
    memset(_nodes, 0, sizeof(_nodes));
    _udp.begin(MEDUSA_UDP_PORT);
    Serial.printf("[Nodes] UDP listening on port %d\n", MEDUSA_UDP_PORT);
}

// Processes one pending UDP packet per call and marks stale nodes offline.
// Should be called every loop() iteration.
void NodeManager::tick() {
    uint32_t now = millis();
    for (int i = 0; i < MEDUSA_MAX_NODES; i++) {
        if (!_nodes[i].active) continue;
        bool wasOnline = _nodes[i].online;
        _nodes[i].online = (now - _nodes[i].lastSeen) < NODE_STALE_MS;
        if (wasOnline && !_nodes[i].online)
            Serial.printf("[Nodes] Node offline: %s\n", _macStr(_nodes[i].mac).c_str());
    }

    int packetSize = _udp.parsePacket();
    if (packetSize <= 0) return;

    uint8_t buf[64];
    int len = _udp.read(buf, sizeof(buf));
    if (len < 1) return;

    IPAddress senderIP = _udp.remoteIP();

    switch (buf[0]) {
        case PKT_NODE_HELLO:
            if (len >= (int)sizeof(PktNodeHello)) {
                PktNodeHello pkt;
                memcpy(&pkt, buf, sizeof(PktNodeHello));
                _handleHello(pkt, senderIP);
            } else {
                Serial.printf("[Nodes] Short hello from %s (%d bytes)\n",
                              senderIP.toString().c_str(), len);
            }
            break;

        default:
            Serial.printf("[Nodes] Unknown packet type 0x%02X from %s\n",
                          buf[0], senderIP.toString().c_str());
            break;
    }
}

// Returns the number of currently online nodes.
int NodeManager::nodeCount() const {
    int count = 0;
    for (int i = 0; i < MEDUSA_MAX_NODES; i++)
        if (_nodes[i].active && _nodes[i].online) count++;
    return count;
}

// Returns a pointer to the Nth online node (0-based), or nullptr if out of range.
NodeInfo* NodeManager::getNode(int index) {
    int seen = 0;
    for (int i = 0; i < MEDUSA_MAX_NODES; i++) {
        if (_nodes[i].active && _nodes[i].online) {
            if (seen == index) return &_nodes[i];
            seen++;
        }
    }
    return nullptr;
}

// Returns a pointer to the node matching the given MAC, or nullptr if not found.
NodeInfo* NodeManager::findByMAC(const uint8_t mac[6]) {
    for (int i = 0; i < MEDUSA_MAX_NODES; i++) {
        if (_nodes[i].active && memcmp(_nodes[i].mac, mac, 6) == 0)
            return &_nodes[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

// Updates (or registers) a node from an incoming hello packet, then immediately
// sends a command back with the current computed output states.
void NodeManager::_handleHello(const PktNodeHello& pkt, const IPAddress& senderIP) {
    NodeInfo* node = findByMAC(pkt.mac);

    if (!node) {
        node = _allocSlot(pkt.mac);
        if (!node) {
            Serial.println("[Nodes] Node table full — ignoring hello");
            return;
        }
        Serial.printf("[Nodes] New node: %s @ %s\n",
                      _macStr(pkt.mac).c_str(), senderIP.toString().c_str());
    }

    if (!node->online)
        Serial.printf("[Nodes] Node back online: %s\n", _macStr(pkt.mac).c_str());

    node->ip          = senderIP;
    node->temperature = pkt.temperature;
    node->humidity    = pkt.humidity;
    node->lastSeen    = millis();
    node->online      = true;

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             pkt.mac[0], pkt.mac[1], pkt.mac[2], pkt.mac[3], pkt.mac[4], pkt.mac[5]);
    uint8_t mask = _resolver ? _resolver(String(macStr), node) : 0x00;
    _sendCommand(senderIP, mask);

    if (_onHello) _onHello();
}

// Builds and sends a PktMasterCmd to the given IP, stamped with current UTC time.
void NodeManager::_sendCommand(const IPAddress& ip, uint8_t outputMask) {
    PktMasterCmd cmd;
    cmd.type       = PKT_MASTER_CMD;
    cmd.outputs    = outputMask;
    cmd.unix_time  = (uint32_t)time(nullptr);

    _udp.beginPacket(ip, MEDUSA_UDP_PORT);
    _udp.write((uint8_t*)&cmd, sizeof(cmd));
    _udp.endPacket();
}

// Finds the first inactive slot in the node table, initialises it with the
// given MAC, and returns a pointer to it. Returns nullptr if the table is full.
NodeInfo* NodeManager::_allocSlot(const uint8_t mac[6]) {
    for (int i = 0; i < MEDUSA_MAX_NODES; i++) {
        if (!_nodes[i].active) {
            memset(&_nodes[i], 0, sizeof(NodeInfo));
            memcpy(_nodes[i].mac, mac, 6);
            _nodes[i].active = true;
            _nodes[i].online = true;
            return &_nodes[i];
        }
    }
    return nullptr;
}

// Formats a 6-byte MAC array as "XX:XX:XX:XX:XX:XX".
String NodeManager::_macStr(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}
