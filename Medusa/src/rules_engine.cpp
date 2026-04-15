#include "rules_engine.h"

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

// Evaluates all rules for a node and returns the 6-bit output bitmask.
// If cfg is nullptr (node has no saved config), all outputs stay OFF.
uint8_t RulesEngine::evaluate(const String& mac, const NodeConfig* cfg, const NodeInfo* node) {
    NodeRuntime* rt = _getOrCreate(mac);
    if (!rt || !cfg || !node) return 0x00;

    float temp     = node->temperature;
    float humidity = node->humidity;
    uint8_t mask   = 0;

    for (int i = 0; i < MEDUSA_NUM_OUTPUTS; i++) {
        // Manual mode: config owns the state, runtime is bypassed entirely
        if (cfg->outputs[i].manual_mode) {
            if (cfg->outputs[i].manual_state) mask |= (1 << i);
            continue;
        }

        uint8_t bit = _evaluateOutput(
            const_cast<OutputConfig&>(cfg->outputs[i]),
            rt->outputs[i], temp, humidity
        );
        if (bit) mask |= (1 << i);
    }

    return mask;
}

// Temporarily overrides an output. Clears when the rule next transitions.
// Ignored if the output is in manual mode.
void RulesEngine::forceOutput(const String& mac, int outputIndex, uint8_t state) {
    if (outputIndex < 0 || outputIndex >= MEDUSA_NUM_OUTPUTS) return;
    NodeRuntime* rt = _getOrCreate(mac);
    if (rt) rt->outputs[outputIndex].force = (int8_t)state;
}

// Removes a force on an output, immediately restoring the rule's computed state.
void RulesEngine::clearForce(const String& mac, int outputIndex) {
    if (outputIndex < 0 || outputIndex >= MEDUSA_NUM_OUTPUTS) return;
    NodeRuntime* rt = _getOrCreate(mac);
    if (rt) rt->outputs[outputIndex].force = -1;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

// Finds or creates a NodeRuntime slot for a given MAC.
NodeRuntime* RulesEngine::_getOrCreate(const String& mac) {
    for (int i = 0; i < _count; i++)
        if (_runtime[i].active && _runtime[i].mac == mac) return &_runtime[i];

    if (_count >= MEDUSA_MAX_NODES) return nullptr;

    NodeRuntime& rt = _runtime[_count++];
    rt = NodeRuntime{};
    rt.mac    = mac;
    rt.active = true;
    for (int i = 0; i < MEDUSA_NUM_OUTPUTS; i++) {
        rt.outputs[i].force       = -1;
        rt.outputs[i].computed    = 0;
        rt.outputs[i].timer_since = millis();
    }
    return &rt;
}

// Evaluates one output: computes rule state, handles force, detects transitions.
uint8_t RulesEngine::_evaluateOutput(OutputConfig& cfg, OutputRuntime& rt,
                                      float temperature, float humidity) {
    uint8_t prev     = rt.computed;
    uint8_t computed = 0;

    switch (cfg.rule_type) {
        case RuleType::Timer:
            computed = _evalTimer(cfg.timer, rt);
            break;
        case RuleType::Threshold:
            computed = _evalThreshold(cfg.threshold, temperature, humidity, prev);
            break;
        default:
            computed = 0;
            break;
    }

    // On rule transition: update computed state and clear any active force.
    // This is what makes force "ephemeral" — it only lasts one rule cycle.
    if (computed != prev) {
        rt.computed = computed;
        rt.force    = -1;
    }

    // Force overrides computed in auto mode
    if (rt.force >= 0) return (uint8_t)rt.force;
    return rt.computed;
}

// Evaluates a timer rule: ON for on_ms, then OFF for off_ms, repeating.
uint8_t RulesEngine::_evalTimer(const TimerRule& rule, OutputRuntime& rt) {
    if (rule.on_ms == 0 && rule.off_ms == 0) return 0;

    uint32_t elapsed = millis() - rt.timer_since;
    uint32_t cycle   = rule.on_ms + rule.off_ms;
    if (cycle == 0) return 0;

    uint32_t phase = elapsed % cycle;
    return (phase < rule.on_ms) ? 1 : 0;
}

// Evaluates a threshold rule against current telemetry.
//
// Two-bound hysteresis model:
//   Normal  (invert=false): ON when sensor < lower_val, OFF when sensor > upper_val.
//   Inverted(invert=true) : ON when sensor > upper_val, OFF when sensor < lower_val.
//
// Each check is conditioned on the current state — this is what gives proper
// hysteresis: the rule only fires a transition when the sensor crosses the
// *relevant* bound, not both bounds at once.
// Returns current unchanged when in the dead band between the two thresholds.
uint8_t RulesEngine::_evalThreshold(const ThresholdRule& rule, float temperature, float humidity, uint8_t current) {
    float sensor = rule.use_humidity ? humidity : temperature;
    if (isnan(sensor)) return 0;   // no data — fail safe to OFF

    if (!rule.invert) {
        // Normal: e.g. turn on heater when too cold, off when warm enough
        if (current && !isnan(rule.upper_val) && sensor > rule.upper_val) return 0;
        if (!current && !isnan(rule.lower_val) && sensor < rule.lower_val) return 1;
    } else {
        // Inverted: e.g. turn on cooler when too hot, off when cool enough
        if (current && !isnan(rule.lower_val) && sensor < rule.lower_val) return 0;
        if (!current && !isnan(rule.upper_val) && sensor > rule.upper_val) return 1;
    }

    return current;  // dead band — hold current state
}
