#pragma once

#include <string>

#include "../../src/BookmarkEntry.h"

class BookOrbitHighlightReader {
 public:
  // Visit legacy bookmark highlights and anchored clippings, preferring legacy notes at shared anchors.
  // A null visitor validates both stores. Callers must preflight before performing uploads:
  // a later corrupt entry can otherwise fail after earlier callbacks have run.
  static bool visit(const std::string& bookPath, bool (*visitor)(void*, const BookmarkEntry&), void* context);
};
