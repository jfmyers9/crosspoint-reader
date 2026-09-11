#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
inline unsigned long millis() { return 100; }
inline void delay(unsigned long) {}
inline void configTzTime(const char*, const char*, const char*) {}
