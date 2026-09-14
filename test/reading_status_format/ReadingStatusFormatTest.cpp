#include <gtest/gtest.h>

#include "ReadingStatusFormat.h"

TEST(ReadingStatusFormat, ShowsKnownStatesAndLeavesUnknownBlank) {
  char text[32];
  formatReadingStatus({}, text, sizeof(text));
  EXPECT_STREQ(text, "");
  formatReadingStatus({ReadingStatus::State::Unread, 0}, text, sizeof(text));
  EXPECT_STREQ(text, "Unread");
  formatReadingStatus({ReadingStatus::State::Reading, 37}, text, sizeof(text));
  EXPECT_STREQ(text, "37%");
  formatReadingStatus({ReadingStatus::State::Finished, 100}, text, sizeof(text));
  EXPECT_STREQ(text, "Finished");
}

TEST(ReadingStatusFormat, BoundsOutputAndTerminates) {
  char text[4] = {'x', 'x', 'x', 'x'};
  formatReadingStatus({ReadingStatus::State::Finished, 100}, text, sizeof(text));
  EXPECT_STREQ(text, "Fin");
  formatReadingStatus({}, text, 0);
  EXPECT_STREQ(text, "Fin");
}
