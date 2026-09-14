#pragma once

#include <cstdint>

struct HostTask;
using TaskHandle_t = HostTask*;
constexpr int pdPASS = 1;
constexpr int pdTRUE = 1;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(ms) (ms)
