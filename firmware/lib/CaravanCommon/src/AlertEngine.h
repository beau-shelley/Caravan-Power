// Rule-based alert evaluation with delay (debounce) and hysteresis.
// Stepped rules (Rule::levels) escalate: each step fires once (RAISED with the
// step's severity and threshold), and steps the value recovers from drop back
// silently, so a 30 % warning doesn't fire again at 29 % but 15 % still does.
// Portable: no Arduino dependencies, time is passed in by the caller.
#pragma once
#include "CaravanModel.h"

namespace cv {

class AlertEngine {
 public:
  enum EventType : uint8_t { RAISED, CLEARED };
  struct Event {
    EventType type;
    Rule rule;  // for RAISED: threshold/sev of the step that fired; action only on the deepest step
    float value;
    const char *msg;
  };
  using Callback = void (*)(const Event &ev, void *ctx);

  // Evaluate every rule against the state, fire callbacks for transitions and
  // rebuild state.alerts[] (sorted most severe first, acknowledgements kept).
  void evaluate(State &s, const RuleSet &rules, uint32_t nowMs, uint32_t unixNow,
                Callback cb = nullptr, void *ctx = nullptr);

  // Acknowledge all active alerts (silences buzzer, keeps them listed).
  void ackAll(State &s);
  bool anyUnackedAlarm() const;
  bool isActive(uint8_t ruleId) const;

 private:
  struct Track {
    uint8_t id = 0;
    bool used = false;
    bool pending = false;
    bool active = false;
    bool acked = false;
    uint32_t pendingSinceMs = 0;
    uint32_t since = 0;
    float value = 0;
    uint8_t sev = 0;
    uint8_t level = 0;       // which step of the rule is active (0 = first)
    bool escPending = false; // a deeper step has tripped, waiting out the delay
    uint32_t escSinceMs = 0;
    char msg[64] = {0};
  };
  Track tracks_[MAX_RULES];
  Track *track(uint8_t id, bool create);
};

void formatAlert(char *buf, size_t n, const Rule &r, float value);
Rule stepRule(const Rule &r, int k);

}  // namespace cv
