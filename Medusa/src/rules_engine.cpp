#include "rules_engine.h"

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

// Evaluates all rules for a node and returns the 6-bit output bitmask.
// If cfg is nullptr (node has no saved config), all outputs stay OFF.
//
// Three-pass outlet engine:
//   Pass 1 — rules write their desired state to rt.target.
//   Pass 2 — outlet engine: if target changed since last tick, copy to current.
//   Pass 3 — replicate outputs mirror their source's current.
//
// The UI toggle writes directly to current via setOutputCurrent().
// That override holds naturally until the rule next changes its target.
uint8_t RulesEngine::evaluate(const String& mac, const NodeConfig* cfg, const NodeInfo* node) {
    NodeRuntime* rt = _getOrCreate(mac);
    if (!rt || !cfg || !node) return 0x00;

    float temp     = node->temperature;
    float humidity = node->humidity;

    // Pass 1: all rules write target (replicate reads source current from previous tick)
    for (int i = 0; i < MEDUSA_NUM_OUTPUTS; i++) {
        OutputRuntime& ort = rt->outputs[i];
        if (cfg->outputs[i].manual_mode) {
            ort.current = cfg->outputs[i].manual_state;
            continue;
        }
        if (cfg->outputs[i].rule_type == RuleType::Replicate) {
            uint8_t src = cfg->outputs[i].replicate.src_output;
            if (src < MEDUSA_NUM_OUTPUTS && src != i)
                ort.target = rt->outputs[src].current;
            continue;
        }
        _evaluateTarget(const_cast<OutputConfig&>(cfg->outputs[i]), ort, temp, humidity);
    }

    // Pass 2: outlet engine — sync current to target on change
    for (int i = 0; i < MEDUSA_NUM_OUTPUTS; i++) {
        if (cfg->outputs[i].manual_mode) continue;
        OutputRuntime& ort = rt->outputs[i];
        if (ort.target != ort.prev_target) {
            ort.current     = ort.target;
            ort.prev_target = ort.target;
        }
    }

    // Build mask from current
    uint8_t mask = 0;
    for (int i = 0; i < MEDUSA_NUM_OUTPUTS; i++)
        if (rt->outputs[i].current) mask |= (1 << i);
    return mask;
}

// Sets an output's current state directly (UI toggle event).
// The override holds until the rule next changes its target.
void RulesEngine::setOutputCurrent(const String& mac, int outputIndex, uint8_t state) {
    if (outputIndex < 0 || outputIndex >= MEDUSA_NUM_OUTPUTS) return;
    NodeRuntime* rt = _getOrCreate(mac);
    if (rt) rt->outputs[outputIndex].current = state;
}

// Returns the current timer phase and remaining ms in that phase.
bool RulesEngine::getTimerState(const String& mac, int outputIndex,
                                 uint8_t& outPhase, uint32_t& outRemainingMs) {
    NodeRuntime* rt = _getOrCreate(mac);
    if (!rt || outputIndex < 0 || outputIndex >= MEDUSA_NUM_OUTPUTS) return false;
    outPhase       = rt->outputs[outputIndex].timer_phase;
    outRemainingMs = rt->outputs[outputIndex].timer_remaining_ms;
    return true;
}

// Sets the timer to a specific phase with the given remaining time.
// Aligns target, prev_target, and current so the outlet engine stays quiet
// until the rule next transitions naturally.
void RulesEngine::setTimerPhase(const String& mac, int outputIndex,
                                 uint8_t phase, uint32_t remaining_ms) {
    if (outputIndex < 0 || outputIndex >= MEDUSA_NUM_OUTPUTS) return;
    NodeRuntime* rt = _getOrCreate(mac);
    if (!rt) return;
    OutputRuntime& ort = rt->outputs[outputIndex];
    ort.timer_phase        = phase;
    ort.timer_remaining_ms = remaining_ms;
    ort.timer_last_tick    = millis();
    ort.target             = phase;
    ort.prev_target        = phase;
    ort.current            = phase;
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
        rt.outputs[i].target             = 0;
        rt.outputs[i].prev_target        = 0;
        rt.outputs[i].current            = 0;
        rt.outputs[i].timer_phase        = 0;
        rt.outputs[i].timer_remaining_ms = 0;
        rt.outputs[i].timer_last_tick    = millis();
    }
    return &rt;
}

// Runs a rule's logic and writes the result to rt.target.
// Rules maintain their own internal state — they never read rt.current.
void RulesEngine::_evaluateTarget(OutputConfig& cfg, OutputRuntime& rt,
                                   float temperature, float humidity) {
    uint8_t computed = 0;
    switch (cfg.rule_type) {
        case RuleType::Timer:
            computed = _evalTimer(cfg.timer, rt);
            break;
        case RuleType::Threshold:
            computed = _evalThreshold(cfg.threshold, temperature, humidity, rt.target);
            break;
        default:
            computed = 0;
            break;
    }
    rt.target = computed;
}

// Evaluates a timer rule as an explicit state machine.
// Initialization: timer_remaining_ms=0 on first call triggers the bootstrap
// switch from phase 0 → 1 (ON). If on_ms=0, re-arms phase 0 (OFF) instead.
// top=0 on a phase means that phase is disabled: control stays with the other.
uint8_t RulesEngine::_evalTimer(const TimerRule& rule, OutputRuntime& rt) {
    if (rule.on_ms == 0 && rule.off_ms == 0) return 0;

    uint32_t now     = millis();
    uint32_t elapsed = now - rt.timer_last_tick;
    rt.timer_last_tick = now;

    if (elapsed >= rt.timer_remaining_ms) {
        uint8_t  next    = rt.timer_phase ^ 1;
        uint32_t nextTop = (next == 1) ? rule.on_ms : rule.off_ms;

        if (nextTop == 0) {
            // Next phase is disabled — re-arm the current phase
            uint32_t curTop = (rt.timer_phase == 1) ? rule.on_ms : rule.off_ms;
            rt.timer_remaining_ms = curTop;
        } else {
            rt.timer_phase        = next;
            rt.timer_remaining_ms = nextTop;
        }
    } else {
        rt.timer_remaining_ms -= elapsed;
    }

    return rt.timer_phase;
}

// Evaluates a threshold rule against current telemetry.
//
// Two-bound hysteresis model:
//   Normal  (invert=false): ON when sensor < lower_val, OFF when sensor > upper_val.
//   Inverted(invert=true) : ON when sensor > upper_val, OFF when sensor < lower_val.
//
// Uses prev (the rule's own last target) for hysteresis — never reads the pin.
// Returns prev unchanged when in the dead band between the two thresholds.
uint8_t RulesEngine::_evalThreshold(const ThresholdRule& rule, float temperature, float humidity, uint8_t prev) {
    float sensor = rule.use_humidity ? humidity : temperature;
    if (isnan(sensor)) return 0;   // no data — fail safe to OFF

    if (!rule.invert) {
        if (prev  && !isnan(rule.upper_val) && sensor > rule.upper_val) return 0;
        if (!prev && !isnan(rule.lower_val) && sensor < rule.lower_val) return 1;
    } else {
        if (prev  && !isnan(rule.lower_val) && sensor < rule.lower_val) return 0;
        if (!prev && !isnan(rule.upper_val) && sensor > rule.upper_val) return 1;
    }

    return prev;  // dead band — hold rule's own state
}
