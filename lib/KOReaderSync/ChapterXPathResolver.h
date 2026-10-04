#pragma once

#include <Epub.h>

#include <cstdint>
#include <memory>
#include <string>

class ChapterXPathResolver {
 public:
  /**
   * Resolve the Nth paragraph in a spine item to its real XHTML ancestry path.
   *
   * Returns a KOReader-compatible path like:
   * /body/DocFragment[8]/body/div[2]/section[1]/p[4]
   *
   * An empty string means parsing failed or the paragraph index was not found.
   */
  static std::string findXPathForParagraph(const std::shared_ptr<Epub>& epub, int spineIndex, uint16_t paragraphIndex);

  /**
   * Resolve a zero-based visible-codepoint offset in a spine item to its real
   * XHTML ancestry path plus text-node offset.
   *
   * Returns a KOReader-compatible path like:
   * /body/DocFragment[8]/body/div[2]/section[1]/p[4]/text()[1].0
   *
   * An empty string means parsing failed or the offset did not resolve inside
   * paragraph/list-item text.
   */
  static std::string findXPathForVisibleTextOffset(const std::shared_ptr<Epub>& epub, int spineIndex,
                                                   uint32_t visibleTextOffset);

  // Resolve a nonempty [start, end) range of renderer-visible Unicode codepoints.
  // Text-node coordinates use current KOReader/CRengine's lString32 codepoints, not UTF-16.
  // No clamping or progress fallback;
  // malformed/unresolvable content returns false and clears both outputs.
  // A nonempty expectedQuote verifies the selected source text (ASCII whitespace
  // collapsed/trimmed, otherwise byte-exact). Quotes/range text over 2048 bytes fail.
  static bool findXPathRangeForVisibleTextOffsets(const std::shared_ptr<Epub>& epub, int spineIndex, uint32_t start,
                                                  uint32_t end, std::string& pos0, std::string& pos1,
                                                  const std::string& expectedQuote = {});

  /**
   * Resolve intra-spine progress to a real XHTML ancestry path plus text offset.
   *
   * Returns a KOReader-compatible path like:
   * /body/DocFragment[8]/body/div[2]/section[1]/p[4]/text().96
   *
   * An empty string means parsing failed or the location could not be resolved.
   */
  static std::string findXPathForProgress(const std::shared_ptr<Epub>& epub, int spineIndex, float intraSpineProgress);
};
