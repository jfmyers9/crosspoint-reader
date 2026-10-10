#pragma once

#include <cstdint>

// WireGuard requires increasing handshake timestamps, not wall-clock accuracy.
// Reserve counters durably before use so resets and clock corrections cannot
// repeat a timestamp with the persisted node identity. Callers serialize access.
class WireGuardClock {
 public:
  bool begin();
  bool writeTimestamp(uint8_t output[12]);

 private:
  bool reserve();
  uint64_t next = 0;
  uint64_t end = 0;
};

// Must succeed before starting MicroLink's networking tasks.
bool prepareWireGuardClock();
