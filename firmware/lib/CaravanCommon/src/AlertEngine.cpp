#include "AlertEngine.h"

#include <cstdio>

namespace cv {

void formatAlert(char *buf, size_t n, const Rule &r, float value) {
  const MetricInfo &mi = metricInfo(r.metric);
  int dec = mi.step < 1.0f ? 1 : 0;
  if (r.metric == M_BAT_V) dec = 2;
  snprintf(buf, n, "%s: %.*f%s (%s %.*f)", r.name, dec, value, mi.unit,
           r.cmp == CMP_GT ? ">" : "<", dec, r.threshold);
}

AlertEngine::Track *AlertEngine::track(uint8_t id, bool create) {
  for (auto &t : tracks_)
    if (t.used && t.id == id) return &t;
  if (!create) return nullptr;
  for (auto &t : tracks_) {
    if (!t.used) {
      t = Track();
      t.used = true;
      t.id = id;
      return &t;
    }
  }
  return nullptr;
}

void AlertEngine::evaluate(State &s, const RuleSet &rules, uint32_t nowMs, uint32_t unixNow,
                           Callback cb, void *ctx) {
  // Drop tracks whose rule was deleted.
  for (auto &t : tracks_) {
    if (!t.used) continue;
    bool exists = false;
    for (int i = 0; i < rules.count; i++) exists |= rules.rules[i].id == t.id;
    if (!exists) {
      if (t.active && cb) {
        Rule gone;
        gone.id = t.id;
        Event ev{CLEARED, gone, t.value, t.msg};
        cb(ev, ctx);
      }
      t = Track();
    }
  }

  for (int i = 0; i < rules.count; i++) {
    const Rule &r = rules.rules[i];
    Track *t = track(r.id, true);
    if (!t) continue;

    float v;
    bool have = metricValue(s, r.metric, v);

    if (!r.enabled) {
      if (t->active && cb) {
        Event ev{CLEARED, r, t->value, t->msg};
        cb(ev, ctx);
      }
      t->active = t->pending = false;
      continue;
    }
    if (!have) {  // sensor offline: hold current state, restart debounce
      t->pending = false;
      continue;
    }

    bool trip = (r.cmp == CMP_LT) ? (v < r.threshold) : (v > r.threshold);
    bool clear = (r.cmp == CMP_LT) ? (v >= r.threshold + r.hysteresis)
                                   : (v <= r.threshold - r.hysteresis);

    if (!t->active) {
      if (trip) {
        if (!t->pending) {
          t->pending = true;
          t->pendingSinceMs = nowMs;
        }
        if (nowMs - t->pendingSinceMs >= (uint32_t)r.delaySec * 1000u) {
          t->active = true;
          t->pending = false;
          t->acked = false;
          t->since = unixNow;
          t->value = v;
          t->sev = r.sev;
          formatAlert(t->msg, sizeof(t->msg), r, v);
          if (cb) {
            Event ev{RAISED, r, v, t->msg};
            cb(ev, ctx);
          }
        }
      } else {
        t->pending = false;
      }
    } else {
      t->value = v;
      t->sev = r.sev;
      formatAlert(t->msg, sizeof(t->msg), r, v);
      if (clear) {
        t->active = false;
        if (cb) {
          Event ev{CLEARED, r, v, t->msg};
          cb(ev, ctx);
        }
      }
    }
  }

  // Rebuild the active list, most severe first, then oldest first.
  s.alertCount = 0;
  for (int sev = SEV_ALARM; sev >= SEV_INFO; sev--) {
    for (auto &t : tracks_) {
      if (!t.used || !t.active || t.sev != sev) continue;
      if (s.alertCount >= MAX_ACTIVE_ALERTS) break;
      ActiveAlert &a = s.alerts[s.alertCount++];
      a.ruleId = t.id;
      a.sev = t.sev;
      a.acked = t.acked;
      a.since = t.since;
      a.value = t.value;
      memcpy(a.msg, t.msg, sizeof(a.msg));
    }
  }
}

void AlertEngine::ackAll(State &s) {
  for (auto &t : tracks_)
    if (t.used && t.active) t.acked = true;
  for (int i = 0; i < s.alertCount; i++) s.alerts[i].acked = true;
}

bool AlertEngine::anyUnackedAlarm() const {
  for (auto &t : tracks_)
    if (t.used && t.active && !t.acked && t.sev == SEV_ALARM) return true;
  return false;
}

bool AlertEngine::isActive(uint8_t id) const {
  for (auto &t : tracks_)
    if (t.used && t.id == id) return t.active;
  return false;
}

}  // namespace cv
