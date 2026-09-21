#pragma once

#include <cstdint>

namespace HighlightRange {

// Half-open visible-codepoint ranges within one spine (not UTF-8 byte offsets).
constexpr bool overlaps(const uint32_t start, const uint32_t end, const uint32_t otherStart,
                        const uint32_t otherEnd) {
  return start < end && otherStart < otherEnd && start < otherEnd && otherStart < end;
}

}  // namespace HighlightRange
