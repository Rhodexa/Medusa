#pragma once
#include <Arduino.h>
#include "config_manager.h"
#include "node_manager.h"

// ---------------------------------------------------------------------------
// Per-output runtime state (not persisted — resets on master reboot)
// ---------------------------------------------------------------------------

struct OutputRuntime {
    uint8_t  computed;      // state the rule currently dictates (0 or 1)
    int8_t   force;         // -1 = no force, 0 = forced OFF, 1 = forced ON
                            // clears automatically on next rule transition
    uint32_t timer_since;   // millis() when current timer phase started
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

    // Force an output ON or OFF until the rule next changes its computed state.
    // Has no effect in manual mode (manual_mode takes priority).
    void forceOutput(const String& mac, int outputIndex, uint8_t state);

    // Clear a force on an output, letting the current computed state take over.
    void clearForce(const String& mac, int outputIndex);

private:
    NodeRuntime _runtime[MEDUSA_MAX_NODES];
    int         _count = 0;

    NodeRuntime* _getOrCreate(const String& mac);

    // Evaluate one output's rule against current telemetry and timer state.
    // Updates runtime.computed and clears force on transition.
    uint8_t _evaluateOutput(OutputConfig& cfg, OutputRuntime& rt,
                            float temperature, float humidity);

    uint8_t _evalTimer(const TimerRule& rule, OutputRuntime& rt);
    uint8_t _evalThreshold(const ThresholdRule& rule, float temperature, float humidity, uint8_t current);
};
