#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// A single bookmark entry — a position in a book.
struct BookmarkEntry {
  static constexpr std::size_t MAX_NAME_LENGTH = 128;
  static constexpr std::size_t MAX_HIGHLIGHT_QUOTE_LENGTH = 2048;

  std::string xpath;    // XPath-like progress string
  std::string summary;  // Page preview, or selected quote for a highlight
  std::string name;     // Optional user-provided label
  float percentage;     // Progress percentage (0.0 to 1.0)

  uint16_t computedSpineIndex = 0;        // Spine index at the time of bookmarking
  uint16_t computedChapterPageCount = 0;  // Total page count of the chapter at the time of bookmarking
  uint16_t computedChapterProgress = 0;   // Number of pages into the chapter at the time of bookmarking

  // Exact visible-codepoint offset of the bookmarked page within its spine. Unlike the page
  // number above it is immune to re-pagination, so it lands on the right page under any
  // font/margin/orientation. Absent (hasVisibleTextOffset == false) for pre-offset bookmarks.
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 0;
  // Exclusive end in the same spine's visible-codepoint coordinates. Zero means a bookmark.
  uint32_t highlightEndOffset = 0;

  bool isHighlight() const { return hasVisibleTextOffset && highlightEndOffset > visibleTextOffset; }
};
