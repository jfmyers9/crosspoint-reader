#pragma once
#include <cstdint>
#define ESP_OK 0
inline int esp_efuse_mac_get_default(uint8_t* m) {
  for (int i = 0; i < 6; ++i) m[i] = i + 1;
  return 0;
}
