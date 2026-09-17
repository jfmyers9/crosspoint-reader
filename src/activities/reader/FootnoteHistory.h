#pragma once

#include <Epub/ReaderRenderSpec.h>

#include <optional>

struct FootnotePosition {
  int spineIndex = 0;
  int pageNumber = 0;
  ReaderRenderSpec renderSpec;
  std::optional<uint32_t> visibleTextOffset;
  int pageCount = 0;

  int progressPageCount(const ReaderRenderSpec& layout) const { return renderSpec == layout ? pageCount : 0; }

  bool matchesPage(const FootnotePosition& page) const {
    return spineIndex == page.spineIndex && renderSpec == page.renderSpec && pageNumber == page.pageNumber;
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
