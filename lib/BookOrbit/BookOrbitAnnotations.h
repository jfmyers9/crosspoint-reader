#pragma once

#include <string>

// One-way export of the current EPUB's highlights. Called with the reader paused.
class BookOrbitAnnotations {
 public:
  enum class Result { Skipped, Complete, Pending, Failed, Unmatched };
#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
  static Result sync(const std::string& bookPath);
#else
  static Result sync(const std::string&) { return Result::Skipped; }
#endif
};
