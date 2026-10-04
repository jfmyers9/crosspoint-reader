#pragma once

#include <cstdint>

class HalGPIO {
 public:
  bool pressed = false;
  bool released = false;

  bool wasAnyPressed() const { return pressed; }
  bool wasAnyReleased() const { return released; }
};
