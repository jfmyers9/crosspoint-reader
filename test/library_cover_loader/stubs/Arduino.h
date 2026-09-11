#pragma once

struct HostEsp {
  unsigned getFreeHeap() const { return 100000; }
};
inline HostEsp ESP;
