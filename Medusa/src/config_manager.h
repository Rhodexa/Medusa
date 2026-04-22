#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "medusa_protocol.h"

// ---------------------------------------------------------------------------
// Rule definitions
// ---------------------------------------------------------------------------

enum class RuleType { None, Timer, Threshold, Replicate };

struct TimerRule {
    uint32_t on_ms;     // how long to stay ON per cycle
    uint32_t off_ms;    // how long to stay OFF per cycle
};

struct ThresholdRule {
    bool   use_humidity;  // false = temperature, true = humidity
    float  lower_val;     // activate threshold  (sensor crosses below this to turn ON)
    float  upper_val;     // deactivate threshold (sensor crosses above this to turn OFF)
    bool   invert;        // swap roles: ON above upper, OFF below lower
};

struct ReplicateRule {
    uint8_t src_output;   // index of the output to mirror (0–5)
};

struct OutputConfig {
    String        label;
    bool          manual_mode;   // true = manual, false = auto (rule-driven)
    uint8_t       manual_state;  // output state when in manual mode
    RuleType      rule_type;
    TimerRule     timer;
    ThresholdRule threshold;
    ReplicateRule replicate;
};

struct NodeConfig {
    String       mac;
    String       label;
    OutputConfig outputs[MEDUSA_NUM_OUTPUTS];
};

// ---------------------------------------------------------------------------
// ConfigManager
// ---------------------------------------------------------------------------

#define CONFIG_PATH "/config.json"
#define MAX_NODES   MEDUSA_MAX_NODES

class ConfigManager {
public:
    // Mount LittleFS (must already be mounted) and load config.json.
    // Creates an empty config if the file doesn't exist yet.
    void begin();

    // Returns a pointer to the config for the given MAC, or nullptr if not found.
    NodeConfig* findNode(const String& mac);

    // Adds a blank NodeConfig for the given MAC, or returns existing one.
    NodeConfig* getOrCreate(const String& mac);

    // Persist the current config to config.json on LittleFS.
    bool save();

    // Serialize the full config to a JSON string (for download / API response).
    String toJSON() const;

    // Replace the entire config from a JSON string (for upload / restore).
    // Returns false if parsing fails.
    bool fromJSON(const String& json);

    int nodeCount() const { return _count; }

    const String& presetName() const { return _name; }
    void setPresetName(const String& n) { _name = n; }

private:
    NodeConfig _nodes[MAX_NODES];
    int        _count = 0;
    String     _name;

    void _parseNode(const JsonObject& obj, NodeConfig& cfg);
    void _parseOutput(const JsonObject& obj, OutputConfig& out);
    void _serializeNode(JsonObject& obj, const NodeConfig& cfg) const;
    void _serializeOutput(JsonObject& obj, const OutputConfig& out) const;

    static RuleType    _ruleTypeFromStr(const char* s);
    static const char* _ruleTypeToStr(RuleType t);
};
