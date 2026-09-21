#pragma once
#include <cstdio>
#include <functional>
#include <string>
// Scope tests require deterministic account separation, not a cryptographic digest.
class MD5Builder {
  std::string value;

 public:
  void begin() { value.clear(); }
  void add(const char* part) { value += part; }
  void calculate() {}
  std::string toString() const {
    char result[33];
    std::snprintf(result, sizeof(result), "%016llx%016llx",
                  static_cast<unsigned long long>(std::hash<std::string>{}(value)),
                  static_cast<unsigned long long>(std::hash<std::string>{}(value + "salt")));
    return result;
  }
};
