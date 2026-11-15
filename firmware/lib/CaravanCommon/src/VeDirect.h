// Victron VE.Direct TEXT protocol parser (byte-fed, checksum verified).
// Works with SmartSolar / BlueSolar MPPT, SmartShunt, BMV etc.
// Frames: "\r\nLABEL\tVALUE" ... "\r\nChecksum\t<byte>", sum of block == 0 mod 256.
// Asynchronous HEX frames (":....\n") may be interleaved and are skipped.
#pragma once
#include <cstdint>
#include <cstring>
#include "CaravanModel.h"

namespace cv {

class VeDirectParser {
 public:
  static constexpr int MAX_FIELDS = 28;
  struct Field {
    char label[10];
    char value[34];
  };

  // Feed one byte. Returns true when a complete, checksum-valid block has
  // just been committed (fields() then holds the new block).
  bool feed(uint8_t b);

  int fieldCount() const { return committedCount_; }
  const Field &field(int i) const { return committed_[i]; }
  const char *get(const char *label) const;

  // Apply the last committed block to a Solar record (MPPT labels).
  void applyTo(Solar &s) const;

  uint32_t goodBlocks() const { return good_; }
  uint32_t badBlocks() const { return bad_; }

 private:
  enum St : uint8_t { IDLE, LABEL, VALUE, CHECKSUM, HEX_FRAME };  // not "HEX": Arduino #defines it
  St st_ = IDLE, beforeHex_ = IDLE;
  uint8_t sum_ = 0;
  char label_[10];
  uint8_t labelLen_ = 0;
  char value_[34];
  uint8_t valueLen_ = 0;
  Field pending_[MAX_FIELDS];
  int pendingCount_ = 0;
  Field committed_[MAX_FIELDS];
  int committedCount_ = 0;
  uint32_t good_ = 0, bad_ = 0;
  void storeField();
};

}  // namespace cv
