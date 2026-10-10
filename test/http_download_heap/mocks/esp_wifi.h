#pragma once
using esp_err_t = int;
enum wifi_ps_type_t { WIFI_PS_NONE, WIFI_PS_MIN_MODEM };
constexpr int ESP_OK = 0;
inline int esp_wifi_get_ps(wifi_ps_type_t* mode) {
  *mode = WIFI_PS_MIN_MODEM;
  return ESP_OK;
}
inline int esp_wifi_set_ps(wifi_ps_type_t) { return ESP_OK; }
