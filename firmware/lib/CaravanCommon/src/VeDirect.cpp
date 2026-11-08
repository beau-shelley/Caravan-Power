#include "VeDirect.h"

#include <cstdlib>

namespace cv {

void VeDirectParser::storeField() {
  if (pendingCount_ >= MAX_FIELDS) return;
  Field &f = pending_[pendingCount_++];
  memcpy(f.label, label_, labelLen_);
  f.label[labelLen_] = 0;
  memcpy(f.value, value_, valueLen_);
  f.value[valueLen_] = 0;
}

bool VeDirectParser::feed(uint8_t b) {
  if (b == ':' && st_ != CHECKSUM && st_ != HEX) {
    beforeHex_ = st_;
    st_ = HEX;
  }
  if (st_ == HEX) {
    if (b == '\n') st_ = beforeHex_;
    return false;  // hex bytes are excluded from the checksum
  }

  sum_ += b;
  switch (st_) {
    case IDLE:
      if (b == '\n') {
        st_ = LABEL;
        labelLen_ = 0;
      }
      break;
    case LABEL:
      if (b == '\t') {
        label_[labelLen_] = 0;
        if (strcmp(label_, "Checksum") == 0) {
          st_ = CHECKSUM;
        } else {
          st_ = VALUE;
          valueLen_ = 0;
        }
      } else if (b == '\r' || b == '\n') {
        labelLen_ = 0;  // empty / malformed line
        st_ = (b == '\n') ? LABEL : IDLE;
      } else if (labelLen_ < sizeof(label_) - 1) {
        label_[labelLen_++] = (char)b;
      }
      break;
    case VALUE:
      if (b == '\r') {
        // wait for \n
      } else if (b == '\n') {
        storeField();
        st_ = LABEL;
        labelLen_ = 0;
      } else if (valueLen_ < sizeof(value_) - 1) {
        value_[valueLen_++] = (char)b;
      }
      break;
    case CHECKSUM: {
      bool ok = (sum_ == 0);
      if (ok) {
        memcpy(committed_, pending_, sizeof(Field) * pendingCount_);
        committedCount_ = pendingCount_;
        good_++;
      } else {
        bad_++;
      }
      pendingCount_ = 0;
      sum_ = 0;
      st_ = IDLE;
      return ok;
    }
    default:
      break;
  }
  return false;
}

const char *VeDirectParser::get(const char *label) const {
  for (int i = 0; i < committedCount_; i++)
    if (strcmp(committed_[i].label, label) == 0) return committed_[i].value;
  return nullptr;
}

void VeDirectParser::applyTo(Solar &s) const {
  const char *v;
  if ((v = get("V"))) s.batV = atol(v) / 1000.0f;
  if ((v = get("I"))) s.batA = atol(v) / 1000.0f;
  if ((v = get("VPV"))) s.pvV = atol(v) / 1000.0f;
  if ((v = get("PPV"))) s.pvW = (float)atol(v);
  if ((v = get("CS"))) s.cs = (uint8_t)atoi(v);
  if ((v = get("ERR"))) s.err = (uint16_t)atoi(v);
  if ((v = get("H19"))) s.yieldTotalKwh = atol(v) / 100.0f;
  if ((v = get("H20"))) s.yieldTodayKwh = atol(v) / 100.0f;
  if ((v = get("H21"))) s.maxWToday = (uint16_t)atoi(v);
}

}  // namespace cv
