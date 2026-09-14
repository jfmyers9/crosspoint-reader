#pragma once

#include <algorithm>
#include <cstring>
#include <string_view>

// Display host/port only, excluding credentials, paths and query data.
inline void copySyncDiagnosticHost(char* output, size_t capacity, std::string_view url) {
  if (!capacity) return;
  const auto scheme = url.find("://");
  const size_t start = scheme == std::string_view::npos ? 0 : scheme + 3;
  const size_t end = url.find_first_of("/?#", start);
  const size_t limit = end == std::string_view::npos ? url.size() : end;
  const auto at = url.rfind('@', limit);
  const size_t host = at != std::string_view::npos && at >= start && at < limit ? at + 1 : start;
  const size_t count = std::min(limit - host, capacity - 1);
  if (count) memcpy(output, url.data() + host, count);
  output[count] = '\0';
  for (size_t i = 0; i < count; ++i) {
    if (static_cast<unsigned char>(output[i]) < 32 || output[i] == 127) output[i] = '?';
  }
}
