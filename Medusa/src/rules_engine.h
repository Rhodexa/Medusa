#pragma once
#include <Arduino.h>
#include "config_manager.h"
#include "node_manager.h"

// ---------------------------------------------------------------------------
// Per-output runtime state (not persisted — resets on master reboot)
// ---------------------------------------------------------------------------

struct OutputRuntime {
    uint8_t  target;             // desired state from the current rule (0 or 1)
    uint8_t  prev_target;        // target at the last outlet-engine tick
    uint8_t  current;            // actual output state sent to the node
                                 // toggle writes here; engine copies target→current on change
    uint8_t  timer_phase;        // 0 = OFF active, 1 = ON active
    uint32_t timer_remaining_ms; // countdown for the active phase
    uint32_t timer_last_tick;    // millis() at the last _evalTimer call
};

struct NodeRuntime {
    String        mac;
    OutputRuntime outputs[MEDUSA_NUM_OUTPUTS];
    bool          active;
};

// ---------------------------------------------------------------------------
// RulesEngine
// ---------------------------------------------------------------------------

class RulesEngine {
public:
    // Evaluate all rules for a node and return the output bitmask to send.
    // Creates runtime state on first call for an unknown MAC.
    uint8_t evaluate(const String& mac, const NodeConfig* cfg, const NodeInfo* node);

    // Immediately set an output's current state (UI toggle event).
    // Has no effect in manual mode. The override holds until the rule next
    // changes its target, at which point the outlet engine re-syncs current.
    void setOutputCurrent(const String& mac, int outputIndex, uint8_t state);

    // Get the current timer phase (1=ON, 0=OFF) and ms remaining in that phase.
    // Returns false if the runtime slot doesn't exist.
    bool getTimerState(const String& mac, int outputIndex,
                       uint8_t& outPhase, uint32_t& outRemainingMs);

    // Jump to a specific timer phase with the given remaining time.
    // Writes directly to runtime state; engine continues naturally from there.
    void setTimerPhase(const String& mac, int outputIndex,
                       uint8_t phase, uint32_t remaining_ms);

private:
    NodeRuntime _runtime[MEDUSA_MAX_NODES];
    int         _count = 0;

    NodeRuntime* _getOrCreate(const String& mac);

    // Compute a rule's desired state and write it to rt.target.
    void _evaluateTarget(OutputConfig& cfg, OutputRuntime& rt,
                         float temperature, float humidity);

    uint8_t _evalTimer(const TimerRule& rule, OutputRuntime& rt);
    uint8_t _evalThreshold(const ThresholdRule& rule, float temperature, float humidity, uint8_t prev);
};
