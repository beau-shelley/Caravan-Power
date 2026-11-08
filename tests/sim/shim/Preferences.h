#pragma once
#include <map>
#include <string>
class Preferences {
 public:
  bool begin(const char *, bool = false) { return true; }
  void end() {}
  int getInt(const char *k, int d = 0) { auto it = m_.find(k); return it == m_.end() ? d : it->second; }
  void putInt(const char *k, int v) { m_[k] = v; }
 private:
  static inline std::map<std::string, int> m_;
};
