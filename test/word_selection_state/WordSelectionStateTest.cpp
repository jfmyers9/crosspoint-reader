#include <gtest/gtest.h>

#include "src/util/WordSelectionState.h"

TEST(WordSelectionStateTest, StartsInvalidAndCannotExtend) {
  WordSelectionState state;
  EXPECT_FALSE(state.valid());
  EXPECT_EQ(state.anchor(), -1);
  EXPECT_EQ(state.selected(), -1);
  EXPECT_EQ(state.first(), -1);
  EXPECT_EQ(state.last(), -1);
  state.beginExtension();
  EXPECT_FALSE(state.extending());
  state.select(0);
  EXPECT_TRUE(state.valid());
  EXPECT_EQ(state.anchor(), 0);
  EXPECT_EQ(state.selected(), 0);
}

TEST(WordSelectionStateTest, MovementWithoutExtensionKeepsSingleWord) {
  WordSelectionState state;
  for (const int index : {4, 5, 6, 2, 0, 0}) {
    state.select(index);
    EXPECT_EQ(state.anchor(), index);
    EXPECT_EQ(state.selected(), index);
    EXPECT_EQ(state.first(), index);
    EXPECT_EQ(state.last(), index);
    EXPECT_FALSE(state.extending());
  }
}

TEST(WordSelectionStateTest, ExtensionRetainsAnchorInEitherDirection) {
  WordSelectionState state;
  state.select(4);
  state.beginExtension();
  for (const int index : {7, 7, 4, 1, 1, 6}) {
    state.select(index);
    EXPECT_TRUE(state.extending());
    EXPECT_EQ(state.anchor(), 4);
    EXPECT_EQ(state.selected(), index);
    EXPECT_EQ(state.first(), index < 4 ? index : 4);
    EXPECT_EQ(state.last(), index > 4 ? index : 4);
    // Opening extension again must not silently move the original anchor.
    state.beginExtension();
    EXPECT_EQ(state.anchor(), 4);
  }
}

TEST(WordSelectionStateTest, RepeatedTouchAndSteppedButtonSelectionAreEquivalent) {
  WordSelectionState touch;
  WordSelectionState buttons;
  touch.select(2);
  buttons.select(2);
  touch.select(5);
  touch.select(5);
  for (const int index : {3, 4, 5}) buttons.select(index);
  EXPECT_EQ(touch.anchor(), buttons.anchor());
  EXPECT_EQ(touch.selected(), buttons.selected());

  touch.beginExtension();
  buttons.beginExtension();
  touch.select(1);
  touch.select(1);
  for (const int index : {4, 3, 2, 1}) buttons.select(index);
  EXPECT_EQ(touch.anchor(), buttons.anchor());
  EXPECT_EQ(touch.selected(), buttons.selected());
  EXPECT_EQ(touch.first(), buttons.first());
  EXPECT_EQ(touch.last(), buttons.last());
}

TEST(WordSelectionStateTest, ResetCollapsesRangeAndEndsExtension) {
  WordSelectionState state;
  state.select(4);
  state.beginExtension();
  state.select(8);
  state.reset(state.selected());
  EXPECT_FALSE(state.extending());
  EXPECT_EQ(state.first(), 8);
  EXPECT_EQ(state.last(), 8);
  state.select(9);
  EXPECT_EQ(state.anchor(), 9);
  state.beginExtension();
  state.select(12);
  state.reset(state.anchor());
  EXPECT_FALSE(state.extending());
  EXPECT_EQ(state.first(), 9);
  EXPECT_EQ(state.last(), 9);
}

TEST(WordSelectionStateTest, InvalidInputClearsBothEndpointsAndExtension) {
  WordSelectionState state;
  for (const int invalid : {-1, -5}) {
    state.select(4);
    state.beginExtension();
    state.select(8);
    state.select(invalid);
    EXPECT_FALSE(state.valid());
    EXPECT_FALSE(state.extending());
    EXPECT_EQ(state.first(), -1);
    EXPECT_EQ(state.last(), -1);
    state.select(3);
    EXPECT_EQ(state.anchor(), 3);
    state.reset(invalid);
    EXPECT_FALSE(state.valid());
    EXPECT_EQ(state.anchor(), -1);
    EXPECT_EQ(state.selected(), -1);
  }
}
