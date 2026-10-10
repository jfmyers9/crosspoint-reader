#if defined(CROSSPOINT_ENABLE_TAILSCALE)
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/sys.h>

#include <cstring>

#include "WireGuardClock.h"

extern "C" {
#include <wireguard-platform.h>
}

namespace {
WireGuardClock handshakeClock;
StaticSemaphore_t clockMutexStorage;
SemaphoreHandle_t clockMutex = nullptr;
}  // namespace

bool prepareWireGuardClock() {
  // Initialize on the activity task, before MicroLink can issue handshakes.
  if (!clockMutex) clockMutex = xSemaphoreCreateMutexStatic(&clockMutexStorage);
  xSemaphoreTake(clockMutex, portMAX_DELAY);
  const bool ready = handshakeClock.begin();
  xSemaphoreGive(clockMutex);
  return ready;
}

extern "C" uint32_t wireguard_sys_now() { return sys_now(); }

extern "C" void wireguard_tai64n_now(uint8_t* output) {
  if (!clockMutex) {
    memset(output, 0, 12);
    return;
  }
  // A mutex, not a critical section: reserving a range may write NVS.
  xSemaphoreTake(clockMutex, portMAX_DELAY);
  handshakeClock.writeTimestamp(output);
  xSemaphoreGive(clockMutex);
}

extern "C" void wireguard_random_bytes(void* bytes, size_t size) { esp_fill_random(bytes, size); }

extern "C" bool wireguard_is_under_load() { return false; }
#endif
