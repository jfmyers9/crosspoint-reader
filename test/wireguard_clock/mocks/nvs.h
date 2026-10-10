#pragma once

#include <cstdint>

using nvs_handle_t = unsigned;
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_FAIL = -1;
constexpr int ESP_ERR_NVS_NOT_FOUND = 1;
constexpr int NVS_READWRITE = 1;

namespace mocknvs {
inline uint64_t stored = 0;
inline uint64_t pending = 0;
inline int commits = 0;
inline int openError = ESP_OK;
inline int readError = ESP_OK;
inline int writeError = ESP_OK;
inline int commitError = ESP_OK;
inline void reset() {
  stored = pending = 0;
  commits = 0;
  openError = readError = writeError = commitError = ESP_OK;
}
}  // namespace mocknvs

inline int nvs_open(const char*, int, nvs_handle_t* handle) {
  *handle = 1;
  return mocknvs::openError;
}
inline int nvs_get_u64(nvs_handle_t, const char*, uint64_t* value) {
  if (mocknvs::readError) return mocknvs::readError;
  if (!mocknvs::stored) return ESP_ERR_NVS_NOT_FOUND;
  *value = mocknvs::stored;
  return ESP_OK;
}
inline int nvs_set_u64(nvs_handle_t, const char*, uint64_t value) {
  mocknvs::pending = value;
  return mocknvs::writeError;
}
inline int nvs_commit(nvs_handle_t) {
  ++mocknvs::commits;
  if (mocknvs::commitError) return mocknvs::commitError;
  mocknvs::stored = mocknvs::pending;
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
