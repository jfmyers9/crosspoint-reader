#pragma once
#include <cstdint>
struct Rtc {
  struct DateTime {
    uint16_t year;
    uint8_t month, day, hour, minute, second, weekday;
  };
  inline static DateTime value{};
  inline static bool present = true;
  inline static bool valid = true;
  bool begin() { return present; }
  bool now(DateTime& out) {
    out = value;
    return valid;
  }
  bool set(const DateTime&) { return true; }
};
