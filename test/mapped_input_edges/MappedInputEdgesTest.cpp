#include <gtest/gtest.h>

#include "MappedInputManager.h"

class GfxRenderer {};

TEST(MappedInputEdges, ForwardsIndependentPhysicalEdgesWithoutConsumingThem) {
  HalGPIO gpio;
  GfxRenderer renderer;
  const MappedInputManager input(gpio, renderer);

  for (const bool pressed : {false, true}) {
    for (const bool released : {false, true}) {
      gpio.pressed = pressed;
      gpio.released = released;
      EXPECT_EQ(input.wasAnyPressed(), pressed);
      EXPECT_EQ(input.wasAnyReleased(), released);
      EXPECT_EQ(input.wasAnyPressed(), pressed);
      EXPECT_EQ(input.wasAnyReleased(), released);
    }
  }
}
