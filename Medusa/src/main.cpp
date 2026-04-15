#include <Arduino.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <ArduinoJson.h>
#include "wifi_manager.h"
#include "node_manager.h"
#include "config_manager.h"
#include "rules_engine.h"
#include "display.h"
#include "status_led.h"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

static WiFiManager   wifiMgr;
static NodeManager   nodeMgr;
static ConfigManager cfgMgr;
static RulesEngine   rules;
static WebServer     server(80);
static Display       display;
static StatusLed     statusLed;

// ---------------------------------------------------------------------------
// Helper: stream a file from LittleFS
// ---------------------------------------------------------------------------

static bool serveFile(const String& path, const char* contentType) {
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    server.streamFile(f, contentType);
    f.close();
    return true;
}

// ---------------------------------------------------------------------------
// Route handlers — WiFi config
// ---------------------------------------------------------------------------

void handleWifiStatus() {
    JsonDocument doc;
    doc["connected"] = wifiMgr.isStaConnected();
    doc["ssid"]      = wifiMgr.staSSID();
    doc["ip"]        = wifiMgr.staIP();
    doc["ap_ssid"]   = wifiMgr.apSSID();

    JsonArray slots = doc["slots"].to<JsonArray>();
    for (int i = 0; i < MEDUSA_MAX_NETWORKS; i++) {
        JsonObject slot = slots.add<JsonObject>();
        slot["ssid"] = wifiMgr.getCredential(i).ssid;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void handleWifiSave() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "Bad JSON"); return; }

    int    slot     = doc["slot"]     | -1;
    String ssid     = doc["ssid"]     | "";
    String password = doc["password"] | "";

    if (slot < 0 || slot >= MEDUSA_MAX_NETWORKS || ssid.isEmpty()) {
        server.send(400, "text/plain", "Invalid slot or empty SSID");
        return;
    }
    wifiMgr.saveCredential(slot, ssid, password);
    server.send(200, "text/plain", "OK");
}

void handleWifiClear() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "Bad JSON"); return; }

    int slot = doc["slot"] | -1;
    if (slot < 0 || slot >= MEDUSA_MAX_NETWORKS) { server.send(400, "text/plain", "Invalid slot"); return; }
    wifiMgr.clearCredential(slot);
    server.send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------------------
// Route handlers — Nodes & telemetry
// ---------------------------------------------------------------------------

// Returns the live status of all online nodes (telemetry + output states).
void handleGetNodes() {
    JsonDocument doc;
    JsonArray arr = doc["nodes"].to<JsonArray>();

    for (int i = 0; i < nodeMgr.nodeCount(); i++) {
        NodeInfo* node = nodeMgr.getNode(i);
        if (!node) continue;

        String mac = "";
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 node->mac[0], node->mac[1], node->mac[2],
                 node->mac[3], node->mac[4], node->mac[5]);
        mac = buf;

        NodeConfig* cfg = cfgMgr.findNode(mac);

        JsonObject obj = arr.add<JsonObject>();
        obj["mac"]         = mac;
        obj["label"]       = cfg ? cfg->label : mac;
        obj["temperature"] = node->temperature;
        obj["humidity"]    = node->humidity;
        obj["last_seen"]   = node->lastSeen;
        obj["outputs"]     = rules.evaluate(mac, cfg, node);  // bitmask
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ---------------------------------------------------------------------------
// Route handlers — Node config (rules, labels, mode)
// ---------------------------------------------------------------------------

// GET /api/node/<mac>/config  — returns the config for one node.
// Auto-creates a blank config for new nodes (not persisted until first save).
void handleGetNodeConfig() {
    String mac = server.pathArg(0);
    NodeConfig* cfg = cfgMgr.getOrCreate(mac);
    if (!cfg) { server.send(500, "text/plain", "Node table full"); return; }

    JsonDocument doc;
    doc["mac"]   = cfg->mac;
    doc["label"] = cfg->label;
    JsonArray outputs = doc["outputs"].to<JsonArray>();
    for (int i = 0; i < MEDUSA_NUM_OUTPUTS; i++) {
        JsonObject o = outputs.add<JsonObject>();
        o["label"]        = cfg->outputs[i].label;
        o["manual_mode"]  = cfg->outputs[i].manual_mode;
        o["manual_state"] = cfg->outputs[i].manual_state;
        o["rule_type"]    = (int)cfg->outputs[i].rule_type;
        if (cfg->outputs[i].rule_type == RuleType::Timer) {
            o["on_ms"]  = cfg->outputs[i].timer.on_ms;
            o["off_ms"] = cfg->outputs[i].timer.off_ms;
        }
        if (cfg->outputs[i].rule_type == RuleType::Threshold) {
            o["use_humidity"] = cfg->outputs[i].threshold.use_humidity;
            o["invert"]       = cfg->outputs[i].threshold.invert;
            if (!isnan(cfg->outputs[i].threshold.lower_val)) o["lower_val"] = cfg->outputs[i].threshold.lower_val;
            if (!isnan(cfg->outputs[i].threshold.upper_val)) o["upper_val"] = cfg->outputs[i].threshold.upper_val;
        }
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// POST /api/node/<mac>/config  — update label and/or output rules for a node.
void handleSetNodeConfig() {
    String mac = server.pathArg(0);
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "Bad JSON"); return; }

    NodeConfig* cfg = cfgMgr.getOrCreate(mac);
    if (!cfg) { server.send(500, "text/plain", "Node table full"); return; }

    if (doc["label"].is<const char*>()) cfg->label = doc["label"].as<String>();

    JsonArray outputs = doc["outputs"].as<JsonArray>();
    if (outputs) {
        int i = 0;
        for (JsonObject o : outputs) {
            if (i >= MEDUSA_NUM_OUTPUTS) break;
            if (o["label"].is<const char*>())      cfg->outputs[i].label        = o["label"].as<String>();
            if (o["manual_mode"].is<bool>())        cfg->outputs[i].manual_mode  = o["manual_mode"];
            if (o["manual_state"].is<int>())        cfg->outputs[i].manual_state = (uint8_t)(int)o["manual_state"];
            if (o["rule_type"].is<int>())           cfg->outputs[i].rule_type    = (RuleType)(int)o["rule_type"];
            if (o["on_ms"].is<uint32_t>())          cfg->outputs[i].timer.on_ms  = o["on_ms"];
            if (o["off_ms"].is<uint32_t>())         cfg->outputs[i].timer.off_ms = o["off_ms"];
            if (o["use_humidity"].is<bool>())       cfg->outputs[i].threshold.use_humidity = o["use_humidity"];
            if (o["invert"].is<bool>())             cfg->outputs[i].threshold.invert      = o["invert"];
            if (o["lower_val"].is<float>())         cfg->outputs[i].threshold.lower_val   = o["lower_val"];
            if (o["upper_val"].is<float>())         cfg->outputs[i].threshold.upper_val   = o["upper_val"];
            i++;
        }
    }

    cfgMgr.save();
    server.send(200, "text/plain", "OK");
}

// POST /api/node/<mac>/force  — temporary force override on one output.
// Body: { "output": 0-5, "state": 0|1 }  or  { "output": 0-5, "clear": true }
void handleForceOutput() {
    String mac = server.pathArg(0);
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "Bad JSON"); return; }

    int output = doc["output"] | -1;
    if (output < 0 || output >= MEDUSA_NUM_OUTPUTS) { server.send(400, "text/plain", "Invalid output"); return; }

    if (doc["clear"] | false)
        rules.clearForce(mac, output);
    else
        rules.forceOutput(mac, output, (uint8_t)(int)(doc["state"] | 0));

    server.send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------------------
// Route handlers — Full config download / upload
// ---------------------------------------------------------------------------

void handleConfigDownload() {
    server.send(200, "application/json", cfgMgr.toJSON());
}

void handleConfigUpload() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }
    if (!cfgMgr.fromJSON(server.arg("plain"))) { server.send(400, "text/plain", "Invalid config JSON"); return; }
    cfgMgr.save();
    server.send(200, "text/plain", "OK");
}

void handleGetPresetName() {
    String out = "{\"name\":\"" + cfgMgr.presetName() + "\"}";
    server.send(200, "application/json", out);
}

void handleSetPresetName() {
    if (!server.hasArg("plain")) { server.send(400, "text/plain", "No body"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "Bad JSON"); return; }
    cfgMgr.setPresetName(doc["name"] | "");
    cfgMgr.save();
    server.send(200, "text/plain", "OK");
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    Serial.println("\n[Medusa] Booting...");

    if (!LittleFS.begin(true))
        Serial.println("[FS] LittleFS mount failed!");
    else
        Serial.println("[FS] LittleFS mounted.");

    statusLed.begin();
    display.begin();
    display.setManagers(&nodeMgr, &cfgMgr, &wifiMgr);

    cfgMgr.begin();
    wifiMgr.begin();

    // WiFi routes
    server.on("/api/wifi/status", HTTP_GET,  handleWifiStatus);
    server.on("/api/wifi/save",   HTTP_POST, handleWifiSave);
    server.on("/api/wifi/clear",  HTTP_POST, handleWifiClear);

    // Node routes
    server.on("/api/nodes",                   HTTP_GET,  handleGetNodes);
    server.on(UriBraces("/api/node/{}/config"),HTTP_GET,  handleGetNodeConfig);
    server.on(UriBraces("/api/node/{}/config"),HTTP_POST, handleSetNodeConfig);
    server.on(UriBraces("/api/node/{}/force"), HTTP_POST, handleForceOutput);

    // Config backup / restore
    server.on("/api/config",      HTTP_GET,  handleConfigDownload);
    server.on("/api/config",      HTTP_POST, handleConfigUpload);
    server.on("/api/config/name", HTTP_GET,  handleGetPresetName);
    server.on("/api/config/name", HTTP_POST, handleSetPresetName);

    // Root — serve index.html (single page app)
    server.on("/", HTTP_GET, []() {
        if (!serveFile("/index.html", "text/html"))
            server.send(503, "text/plain", "Upload filesystem image first");
    });

    server.begin();
    Serial.println("[Web] Server started. Connect to 192.168.4.1");

    nodeMgr.setOutputResolver([](const String& mac, const NodeInfo* node) -> uint8_t {
        NodeConfig* cfg = cfgMgr.findNode(mac);
        return rules.evaluate(mac, cfg, node);
    });
    nodeMgr.setHelloCallback([]() {
        statusLed.triggerGreenFlash();
    });
    nodeMgr.begin();
}

void loop() {
    server.handleClient();
    wifiMgr.tick();
    nodeMgr.tick();
    statusLed.setNoNodes(nodeMgr.nodeCount() == 0);
    statusLed.tick();
    display.tick();
}
