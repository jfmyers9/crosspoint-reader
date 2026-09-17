#pragma once

#include <Epub/ReaderRenderSpec.h>

#include <optional>

struct FootnotePosition {
  int spineIndex = 0;
  int pageNumber = 0;
  ReaderRenderSpec renderSpec;
  std::optional<uint32_t> visibleTextOffset;
  // Exclusive end of the original page; absent at an unfinished build's frontier.
  std::optional<uint32_t> nextVisibleTextOffset;
  int pageCount = 0;

  std::optional<uint32_t> returnOffset(const ReaderRenderSpec& layout) const {
    return renderSpec == layout ? std::nullopt : visibleTextOffset;
  }

  int progressPageCount(const ReaderRenderSpec& layout) const { return renderSpec == layout ? pageCount : 0; }

  bool matchesPage(const FootnotePosition& page) const {
    if (spineIndex != page.spineIndex) return false;
    // Image-only pages can share text offsets, so retain exact page identity when possible.
    if (renderSpec == page.renderSpec) return pageNumber == page.pageNumber;
    if (!visibleTextOffset || !page.visibleTextOffset) return false;
    if (*visibleTextOffset == *page.visibleTextOffset) return true;
    if (*visibleTextOffset < *page.visibleTextOffset) {
      return nextVisibleTextOffset && *page.visibleTextOffset < *nextVisibleTextOffset;
    }
    return page.nextVisibleTextOffset && *visibleTextOffset < *page.nextVisibleTextOffset;
  }
};

class FootnoteHistory {
  static constexpr int MAX_DEPTH = 3;
  FootnotePosition positions[MAX_DEPTH];
  int depth = 0;

 public:
  bool empty() const { return depth == 0; }
  void clear() { depth = 0; }
  const FootnotePosition* origin() const { return empty() ? nullptr : &positions[0]; }

  void push(const FootnotePosition& position) {
    if (depth < MAX_DEPTH) positions[depth++] = position;
  }

  std::optional<FootnotePosition> pop() {
    if (empty()) return std::nullopt;
    return positions[--depth];
  }

  void unwind(const FootnotePosition& page) {
    for (int i = 0; i < depth; ++i) {
      if (positions[i].matchesPage(page)) {
        depth = i;
        break;
      }
    }
  }
};
