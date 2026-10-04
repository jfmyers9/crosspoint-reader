#pragma once

class BookOrbitAnnotationsClient {
 public:
  // pos0/pos1 must come from ChapterXPathResolver, never synthetic page positions.
  // Empty note is explicit (clears a previous note); nullptr is invalid.
  static bool upload(const char* bookHash, const char* datetime, const char* datetimeUpdated, const char* pos0,
                     const char* pos1, const char* text, const char* note);
  static bool lastUploadWasUnmatched();
};
