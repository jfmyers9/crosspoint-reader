#pragma once

#include <cstdint>

class BookOrbitAnnotationsClient {
 public:
  // pos0/pos1 must come from ChapterXPathResolver, never synthetic page positions.
  // Empty note is explicit (clears a previous note); nullptr is invalid.
  // An empty chapter or zero page number means unavailable and is omitted.
  static bool upload(const char* bookHash, const char* datetime, const char* datetimeUpdated, const char* pos0,
                     const char* pos1, const char* text, const char* note, const char* chapter, uint32_t pageNumber);
  static bool lastUploadWasUnmatched();
};
