#include "WireGuardClock.h"

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
#include <Logging.h>
#include <nvs.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace {
// Start beyond the old uptime-based timestamps, without needing working NTP.
constexpr uint64_t INITIAL_COUNTER = 1735689600ULL * 1000000;  // 2025-01-01, microseconds
constexpr uint64_t COUNTERS_PER_RESERVATION = 1000000;
constexpr uint64_t TAI_OFFSET = 0x400000000000000AULL;
}  // namespace

bool WireGuardClock::reserve() {
  nvs_handle_t handle;
  esp_err_t error = nvs_open("cpwgclock", NVS_READWRITE, &handle);
  if (error != ESP_OK) {
    LOG_ERR("TAIL", "Could not open handshake clock: %d", error);
    return false;
  }
  uint64_t persistedEnd = 0;
  error = nvs_get_u64(handle, "end", &persistedEnd);
  if (error == ESP_ERR_NVS_NOT_FOUND) error = ESP_OK;
  const uint64_t start = std::max(INITIAL_COUNTER, std::max(end, persistedEnd));
  if (start > std::numeric_limits<uint64_t>::max() - COUNTERS_PER_RESERVATION) error = ESP_FAIL;
  const uint64_t reservedEnd = start + COUNTERS_PER_RESERVATION;
  if (error == ESP_OK) error = nvs_set_u64(handle, "end", reservedEnd);
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  if (error != ESP_OK) {
    LOG_ERR("TAIL", "Could not persist handshake clock: %d", error);
    return false;
  }
  next = start;
  end = reservedEnd;
  return true;
}

bool WireGuardClock::begin() { return next < end || reserve(); }

bool WireGuardClock::writeTimestamp(uint8_t output[12]) {
  if (!begin()) {
    // Fail closed: peers reject this rather than accepting a reused timestamp.
    memset(output, 0, 12);
    return false;
  }
  const uint64_t counter = next++;
  const uint64_t seconds = TAI_OFFSET + counter / 1000000;
  const uint32_t nanos = static_cast<uint32_t>(counter % 1000000) * 1000;
  for (int i = 0; i < 8; ++i) output[i] = static_cast<uint8_t>(seconds >> (56 - i * 8));
  for (int i = 0; i < 4; ++i) output[8 + i] = static_cast<uint8_t>(nanos >> (24 - i * 8));
  return true;
}
#endif
