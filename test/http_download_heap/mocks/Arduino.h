#pragma once
#include <cstdint>
struct EspStub {
  uint32_t freeHeap = 16000;
  uint32_t maxAlloc = 8000;
  uint32_t getFreeHeap() const { return freeHeap; }
  uint32_t getMaxAllocHeap() const { return maxAlloc; }
};
inline EspStub ESP;
