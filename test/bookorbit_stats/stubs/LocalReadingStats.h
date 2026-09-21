#pragma once

#include <string>

class LocalReadingStats {
 public:
  static inline unsigned begins = 0, pages = 0, suspends = 0, pauses = 0, checkpoints = 0;
  static inline std::string lastBook;
  static void reset() {
    begins = pages = suspends = pauses = checkpoints = 0;
    lastBook.clear();
  }
  static void beginBook(const std::string& path) {
    ++begins;
    lastBook = path;
  }
  static void showPage(float) { ++pages; }
  static void suspend() { ++suspends; }
  static void pause() { ++pauses; }
  static void checkpoint() { ++checkpoints; }
};
