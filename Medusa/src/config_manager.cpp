#include "config_manager.h"
#include <LittleFS.h>

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

// Loads config.json from LittleFS. Creates an empty config if absent.
void ConfigManager::begin() {
    _count = 0;
    if (!LittleFS.exists(CONFIG_PATH)) {
        Serial.println("[Config] No config.json found — starting empty.");
        return;
    }

    File f = LittleFS.open(CONFIG_PATH, "r");
    if (!f) {
        Serial.println("[Config] Failed to open config.json");
        return;
    }

    String json = f.readString();
    f.close();

    if (!fromJSON(json))
        Serial.println("[Config] config.json parse error — starting empty.");
    else
        Serial.printf("[Config] Loaded %d node config(s).\n", _count);
}

// Returns a pointer to the config for the given MAC, or nullptr if not found.
NodeConfig* ConfigManager::findNode(const String& mac) {
    for (int i = 0; i < _count; i++)
        if (_nodes[i].mac.equalsIgnoreCase(mac)) return &_nodes[i];
    return nullptr;
}

// Returns an existing NodeConfig for the MAC, or creates a blank one.
NodeConfig* ConfigManager::getOrCreate(const String& mac) {
    NodeConfig* existing = findNode(mac);
    if (existing) return existing;

    if (_count >= MAX_NODES) {
        Serial.println("[Config] Node table full");
        return nullptr;
    }

    NodeConfig& cfg = _nodes[_count++];
    cfg = NodeConfig{};
    cfg.mac   = mac;
    cfg.label = "Node " + mac.substring(mac.length() - 5);  // e.g. "Node E:D8"

    for (int i = 0; i < MEDUSA_NUM_OUTPUTS; i++) {
        cfg.outputs[i].label       = "Output " + String(i);
        cfg.outputs[i].manual_mode = false;
        cfg.outputs[i].manual_state = 0;
        cfg.outputs[i].rule_type   = RuleType::None;
    }
    return &cfg;
}

// Writes the current config to config.json on LittleFS.
bool ConfigManager::save() {
    File f = LittleFS.open(CONFIG_PATH, "w");
    if (!f) {
        Serial.println("[Config] Failed to open config.json for writing");
        return false;
    }
    f.print(toJSON());
    f.close();
    Serial.println("[Config] Saved.");
    return true;
}

// Serializes the full config to a compact JSON string.
String ConfigManager::toJSON() const {
    JsonDocument doc;
    if (_name.length()) doc["name"] = _name;
    JsonArray nodes = doc["nodes"].to<JsonArray>();
    for (int i = 0; i < _count; i++) {
        JsonObject obj = nodes.add<JsonObject>();
        _serializeNode(obj, _nodes[i]);
    }
    String out;
    serializeJson(doc, out);
    return out;
}

// Replaces the entire config from a JSON string. Returns false on parse error.
bool ConfigManager::fromJSON(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;

    _name = doc["name"] | "";
    JsonArray nodes = doc["nodes"].as<JsonArray>();
    if (!nodes) return false;

    _count = 0;
    for (JsonObject obj : nodes) {
        if (_count >= MAX_NODES) break;
        _parseNode(obj, _nodes[_count++]);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Private — serialization
// ---------------------------------------------------------------------------

void ConfigManager::_parseNode(const JsonObject& obj, NodeConfig& cfg) {
    cfg.mac   = obj["mac"]   | "";
    cfg.label = obj["label"] | "";

    JsonArray outputs = obj["outputs"].as<JsonArray>();
    int i = 0;
    for (JsonObject o : outputs) {
        if (i >= MEDUSA_NUM_OUTPUTS) break;
        _parseOutput(o, cfg.outputs[i++]);
    }
    // Initialise any outputs not present in JSON with safe defaults
    for (; i < MEDUSA_NUM_OUTPUTS; i++) {
        cfg.outputs[i] = OutputConfig{};
        cfg.outputs[i].label     = "Output " + String(i);
        cfg.outputs[i].rule_type = RuleType::None;
    }
}

void ConfigManager::_parseOutput(const JsonObject& obj, OutputConfig& out) {
    out.label        = obj["label"]        | "";
    out.manual_mode  = obj["manual_mode"]  | false;
    out.manual_state = obj["manual_state"] | 0;
    out.rule_type    = _ruleTypeFromStr(obj["rule_type"] | "none");

    if (out.rule_type == RuleType::Timer) {
        out.timer.on_ms  = obj["timer"]["on_ms"]  | 0;
        out.timer.off_ms = obj["timer"]["off_ms"] | 0;
    }

    if (out.rule_type == RuleType::Threshold) {
        out.threshold.use_humidity = obj["threshold"]["use_humidity"] | false;
        out.threshold.lower_val    = obj["threshold"]["lower_val"]    | (float)NAN;
        out.threshold.upper_val    = obj["threshold"]["upper_val"]    | (float)NAN;
        out.threshold.invert       = obj["threshold"]["invert"]       | false;
    }

    if (out.rule_type == RuleType::Replicate) {
        out.replicate.src_output = obj["replicate"]["src_output"] | (uint8_t)0;
    }
}

void ConfigManager::_serializeNode(JsonObject& obj, const NodeConfig& cfg) const {
    obj["mac"]   = cfg.mac;
    obj["label"] = cfg.label;

    JsonArray outputs = obj["outputs"].to<JsonArray>();
    for (int i = 0; i < MEDUSA_NUM_OUTPUTS; i++) {
        JsonObject o = outputs.add<JsonObject>();
        _serializeOutput(o, cfg.outputs[i]);
    }
}

void ConfigManager::_serializeOutput(JsonObject& obj, const OutputConfig& out) const {
    obj["label"]        = out.label;
    obj["manual_mode"]  = out.manual_mode;
    obj["manual_state"] = out.manual_state;
    obj["rule_type"]    = _ruleTypeToStr(out.rule_type);

    if (out.rule_type == RuleType::Timer) {
        JsonObject t  = obj["timer"].to<JsonObject>();
        t["on_ms"]    = out.timer.on_ms;
        t["off_ms"]   = out.timer.off_ms;
    }

    if (out.rule_type == RuleType::Threshold) {
        JsonObject t      = obj["threshold"].to<JsonObject>();
        t["use_humidity"] = out.threshold.use_humidity;
        t["invert"]       = out.threshold.invert;
        if (!isnan(out.threshold.lower_val)) t["lower_val"] = out.threshold.lower_val;
        if (!isnan(out.threshold.upper_val)) t["upper_val"] = out.threshold.upper_val;
    }

    if (out.rule_type == RuleType::Replicate) {
        JsonObject r      = obj["replicate"].to<JsonObject>();
        r["src_output"]   = out.replicate.src_output;
    }
}

// ---------------------------------------------------------------------------
// Private — helpers
// ---------------------------------------------------------------------------

RuleType ConfigManager::_ruleTypeFromStr(const char* s) {
    if (strcmp(s, "timer")     == 0) return RuleType::Timer;
    if (strcmp(s, "threshold") == 0) return RuleType::Threshold;
    if (strcmp(s, "replicate") == 0) return RuleType::Replicate;
    return RuleType::None;
}

const char* ConfigManager::_ruleTypeToStr(RuleType t) {
    switch (t) {
        case RuleType::Timer:     return "timer";
        case RuleType::Threshold: return "threshold";
        case RuleType::Replicate: return "replicate";
        default:                  return "none";
    }
}
