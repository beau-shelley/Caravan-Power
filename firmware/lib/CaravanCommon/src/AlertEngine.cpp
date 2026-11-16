#include "AlertEngine.h"

#include <cstdio>

namespace cv {

void formatAlert(char *buf, size_t n, const Rule &r, float value) {
  if (r.metric == M_THEFT) {  // 10 + cause: entry delay, 20 + cause: alarm
    int v = (int)value;
    if (v >= 20) snprintf(buf, n, "%s: %s", r.name, secureCauseText((uint8_t)(v - 20)));
    else snprintf(buf, n, "%s: movement - disarm within 5 min", r.name);  // short: fits the banner
    return;
  }
  const MetricInfo &mi = metricInfo(r.metric);
  int dec = mi.step < 1.0f ? 1 : 0;
  if (r.metric == M_BAT_V) dec = 2;
  snprintf(buf, n, "%s: %.*f%s (%s %.*f)", r.name, dec, value, mi.unit,
           r.cmp == CMP_GT ? ">" : "<", dec, r.threshold);
}

// The rule as seen at step k: that step's threshold and severity, and the
// automatic action only on the deepest step.
Rule stepRule(const Rule &r, int k) {
  Rule lr = r;
  lr.threshold = r.levelThreshold(k);
  lr.sev = r.levelSev(k);
  if (k < r.levelCount) lr.action = ACT_NONE;
  return lr;
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

    // Deepest step currently tripped (-1 = none). Steps are strictly deeper,
    // so the tripped ones are always a prefix.
    auto tripped = [&](float thr) { return r.cmp == CMP_LT ? v < thr : v > thr; };
    auto recovered = [&](float thr) {
      return r.cmp == CMP_LT ? v >= thr + r.hysteresis : v <= thr - r.hysteresis;
    };
    int deepest = -1;
    for (int k = 0; k <= r.levelCount; k++)
      if (tripped(r.levelThreshold(k))) deepest = k;
    const uint32_t delayMs = (uint32_t)r.delaySec * 1000u;

    auto fire = [&](int k) {
      Rule lr = stepRule(r, k);
      t->level = (uint8_t)k;
      t->sev = lr.sev;
      t->acked = false;
      t->since = unixNow;
      t->value = v;
      formatAlert(t->msg, sizeof(t->msg), lr, v);
      if (cb) {
        Event ev{RAISED, lr, v, t->msg};
        cb(ev, ctx);
      }
    };

    if (!t->active) {
      if (deepest >= 0) {
        if (!t->pending) {
          t->pending = true;
          t->pendingSinceMs = nowMs;
        }
        if (nowMs - t->pendingSinceMs >= delayMs) {
          t->active = true;
          t->pending = false;
          t->escPending = false;
          fire(deepest);
        }
      } else {
        t->pending = false;
      }
    } else {
      t->value = v;
      // Step down (silently) past any steps the value has recovered from, so
      // they can fire again if it falls back.
      while (t->level > 0 && recovered(r.levelThreshold(t->level))) t->level--;
      if (t->level > r.levelCount) t->level = r.levelCount;  // rule was edited
      // Step up when a deeper step has held for the rule's delay.
      if (deepest > (int)t->level) {
        if (!t->escPending) {
          t->escPending = true;
          t->escSinceMs = nowMs;
        }
        if (nowMs - t->escSinceMs >= delayMs) {
          t->escPending = false;
          fire(deepest);
        }
      } else {
        t->escPending = false;
      }
      Rule lr = stepRule(r, t->level);
      t->sev = lr.sev;
      formatAlert(t->msg, sizeof(t->msg), lr, v);
      if (t->level == 0 && recovered(r.threshold)) {
        t->active = false;
        t->escPending = false;
        t->level = 0;
        if (cb) {
          Event ev{CLEARED, r, v, t->msg};
          cb(ev, ctx);
        }
      }
    }
  }

  // Rebuild the active list, most severe first, then oldest first.
  s.alertCount = 0;
  for (int sev = SEV_CRIT; sev >= SEV_INFO; sev--) {
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
    if (t.used && t.active && !t.acked && t.sev >= SEV_ALARM) return true;
  return false;
}

bool AlertEngine::isActive(uint8_t id) const {
  for (auto &t : tracks_)
    if (t.used && t.id == id) return t.active;
  return false;
}

}  // namespace cv
