// Rule-based alert evaluation with delay (debounce) and hysteresis.
// Portable: no Arduino dependencies, time is passed in by the caller.
#pragma once
#include "CaravanModel.h"

namespace cv {

class AlertEngine {
 public:
  enum EventType : uint8_t { RAISED, CLEARED };
  struct Event {
    EventType type;
    Rule rule;
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
    char msg[64] = {0};
  };
  Track tracks_[MAX_RULES];
  Track *track(uint8_t id, bool create);
};

void formatAlert(char *buf, size_t n, const Rule &r, float value);

}  // namespace cv
