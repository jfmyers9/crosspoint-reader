#pragma once
#include <cstdint>
extern int64_t stubMonotonicMs;
inline int64_t esp_timer_get_time() { return stubMonotonicMs * 1000; }
