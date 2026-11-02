#pragma once

#include <string>

#include "../../src/BookmarkEntry.h"

class BookOrbitHighlightReader {
 public:
  // Visit legacy bookmark highlights and anchored clippings, preferring legacy notes at shared anchors.
  // A null visitor validates both stores. Callers must preflight before performing uploads:
  // a later corrupt entry can otherwise fail after earlier callbacks have run.
  // The chapter title is borrowed for the callback; legacy bookmarks have no saved title.
  using Visitor = bool (*)(void*, const BookmarkEntry&, const char* chapter);
  static bool visit(const std::string& bookPath, Visitor visitor, void* context);
};
