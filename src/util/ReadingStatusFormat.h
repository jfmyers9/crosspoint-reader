#pragma once

#include <I18n.h>
#include <ReadingStatus.h>

#include <cstdio>

inline void formatReadingStatus(const ReadingStatus::Status& status, char* output, size_t capacity) {
  switch (status.state) {
    case ReadingStatus::State::Unread:
      snprintf(output, capacity, "%s", tr(STR_READING_UNREAD));
      break;
    case ReadingStatus::State::Finished:
      snprintf(output, capacity, "%s", tr(STR_READING_FINISHED));
      break;
    case ReadingStatus::State::Reading:
      snprintf(output, capacity, tr(STR_READING_PERCENT_FORMAT), static_cast<unsigned>(status.percent));
      break;
    default:
      if (capacity) output[0] = '\0';
      break;
  }
}
