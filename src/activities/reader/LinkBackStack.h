#pragma once

#include <CrossPointPosition.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>

// Saved reader position captured before following an internal EPUB link (such
// as an endnote, footnote, or in-book TOC link). Stores enough metadata to both
// restore the page accurately after layout changes and map a footnote
// excursion's main-text anchor to a KOReader XPath/percentage.
struct SavedLinkPosition {
  uint32_t visibleTextOffset = 0;
  uint16_t spineIndex = 0;
  uint16_t pageNumber = 0;
  uint16_t totalPages = 0;
  uint16_t paragraphIndex = 0;
  bool hasVisibleTextOffset = false;
  bool hasParagraphIndex = false;
  bool anchorsSync = true;

  CrossPointPosition toCrossPointPosition() const {
    CrossPointPosition pos = {spineIndex, pageNumber, totalPages};
    pos.hasResolvedSpineIndex = true;
    // Legacy v1 records lack totalPages until hydrated from the section cache;
    // avoid comparing unmapped page numbers against remote progress.
    pos.hasMappedPage = totalPages > 0;
    pos.visibleTextOffset = visibleTextOffset;
    pos.hasVisibleTextOffset = hasVisibleTextOffset;
    pos.paragraphIndex = paragraphIndex;
    pos.hasParagraphIndex = hasParagraphIndex;
    return pos;
  }

  static SavedLinkPosition fromCrossPointPosition(const CrossPointPosition& pos, const bool anchorsSync = true) {
    SavedLinkPosition saved;
    saved.spineIndex = static_cast<uint16_t>(std::clamp(pos.spineIndex, 0, 0xFFFF));
    saved.pageNumber = static_cast<uint16_t>(std::clamp(pos.pageNumber, 0, 0xFFFF));
    saved.totalPages = static_cast<uint16_t>(std::clamp(pos.totalPages, 0, 0xFFFF));
    saved.hasVisibleTextOffset = pos.hasVisibleTextOffset;
    saved.visibleTextOffset = pos.hasVisibleTextOffset ? pos.visibleTextOffset : 0;
    saved.hasParagraphIndex = pos.hasParagraphIndex;
    saved.paragraphIndex = pos.hasParagraphIndex ? pos.paragraphIndex : 0;
    saved.anchorsSync = anchorsSync;
    return saved;
  }
};

// Fixed-capacity link/footnote back-stack persisted in `<cachePath>/links.bin`.
// Entries with `anchorsSync == true` mark temporary excursions (such as
// footnotes/endnotes) so KOReader sync reports the oldest anchoring main-text
// position instead of an endnote spine at the back of the book. Non-anchoring
// entries (such as links to TOC chapters) still support Back navigation without
// pinning sync progress.
class LinkBackStack {
 public:
  static constexpr int MAX_DEPTH = 3;
  // High bit distinguishes v2 from legacy v1, whose first byte is depth (1..3).
  static constexpr uint8_t V2_MAGIC = 0x82;
  static constexpr size_t V1_ENTRY_BYTES = 4;
  static constexpr size_t V2_ENTRY_BYTES = 13;
  static constexpr size_t MAX_SERIALIZED_BYTES = 2 + MAX_DEPTH * V2_ENTRY_BYTES;

  bool empty() const { return count == 0; }
  int depth() const { return count; }
  void clear() { count = 0; }
  SavedLinkPosition& at(const int index) { return entries[index]; }
  const SavedLinkPosition& at(const int index) const { return entries[index]; }

  bool hasSyncAnchor() const { return firstSyncAnchorIndex() >= 0; }

  std::optional<SavedLinkPosition> syncAnchor() const {
    const int idx = firstSyncAnchorIndex();
    if (idx < 0) return std::nullopt;
    return entries[idx];
  }

  // Records a link jump from `origin` to `targetSpineIndex` (and `targetPage`
  // when already known on the same spine).
  // Unwinds to a matching ancestor and returns false when following an EPUB's
  // built-in return link across spines or back to the same-spine call site
  // (within ±1 page). Otherwise pushes `origin` and returns true.
  bool followLink(const CrossPointPosition& origin, const int targetSpineIndex,
                  const std::optional<int> targetPage = std::nullopt, const bool anchorsSync = true) {
    if (count > 0 && targetSpineIndex >= 0) {
      if (targetSpineIndex != origin.spineIndex) {
        for (int i = count - 1; i >= 0; --i) {
          if (static_cast<int>(entries[i].spineIndex) == targetSpineIndex) {
            count = i;
            return false;
          }
        }
      } else if (targetPage.has_value()) {
        for (int i = count - 1; i >= 0; --i) {
          if (static_cast<int>(entries[i].spineIndex) == targetSpineIndex &&
              std::abs(static_cast<int>(entries[i].pageNumber) - *targetPage) <= 1) {
            count = i;
            return false;
          }
        }
      }
    }

    if (count == MAX_DEPTH) {
      // Pin the oldest sync anchor (if it sits at entries[0]) by evicting
      // entries[1]; otherwise evict the oldest entry at entries[0].
      const int evictStart = (firstSyncAnchorIndex() == 0 && MAX_DEPTH > 1) ? 1 : 0;
      for (int i = evictStart; i + 1 < MAX_DEPTH; ++i) {
        entries[i] = entries[i + 1];
      }
      count = MAX_DEPTH - 1;
    }

    entries[count++] = SavedLinkPosition::fromCrossPointPosition(origin, anchorsSync);
    return true;
  }

  std::optional<SavedLinkPosition> pop() {
    if (count <= 0) return std::nullopt;
    return entries[--count];
  }

  // Returns the oldest anchoring main-text position while inside a footnote
  // excursion, or `livePosition` when no sync anchor is active.
  CrossPointPosition syncPosition(const CrossPointPosition& livePosition) const {
    const int idx = firstSyncAnchorIndex();
    return idx >= 0 ? entries[idx].toCrossPointPosition() : livePosition;
  }

  // V2 wire format: [magic=0x82][depth:u8] followed by depth x 13-byte LE records:
  //   u16 spineIndex, u16 pageNumber, u16 totalPages, u8 flags,
  //   u32 visibleTextOffset, u16 paragraphIndex.
  size_t serialize(uint8_t* out, const size_t capacity) const {
    if (!out || count <= 0) return 0;
    const size_t needed = 2 + static_cast<size_t>(count) * V2_ENTRY_BYTES;
    if (capacity < needed) return 0;

    size_t pos = 0;
    out[pos++] = V2_MAGIC;
    out[pos++] = static_cast<uint8_t>(count);
    for (int i = 0; i < count; ++i) {
      const SavedLinkPosition& e = entries[i];
      writeU16(out + pos, e.spineIndex);
      pos += 2;
      writeU16(out + pos, e.pageNumber);
      pos += 2;
      writeU16(out + pos, e.totalPages);
      pos += 2;
      const uint8_t flags = (e.hasVisibleTextOffset ? FLAG_VISIBLE_OFFSET : 0) |
                            (e.hasParagraphIndex ? FLAG_PARAGRAPH_INDEX : 0) |
                            (e.anchorsSync ? FLAG_ANCHORS_SYNC : 0);
      out[pos++] = flags;
      writeU32(out + pos, e.hasVisibleTextOffset ? e.visibleTextOffset : 0);
      pos += 4;
      writeU16(out + pos, e.hasParagraphIndex ? e.paragraphIndex : 0);
      pos += 2;
    }
    return pos;
  }

  // Decodes either v2 or legacy v1 format. Resets the stack on invalid input.
  bool deserialize(const uint8_t* data, const size_t size, const int spineCount, bool* wasLegacyV1 = nullptr) {
    count = 0;
    if (wasLegacyV1) *wasLegacyV1 = false;
    if (!data || size == 0 || spineCount <= 0) return false;

    if (size >= 2 && data[0] == V2_MAGIC) {
      const int depth = data[1];
      if (depth < 1 || depth > MAX_DEPTH || size != 2 + static_cast<size_t>(depth) * V2_ENTRY_BYTES) {
        return false;
      }
      SavedLinkPosition decoded[MAX_DEPTH] = {};
      for (int i = 0; i < depth; ++i) {
        const uint8_t* p = data + 2 + static_cast<size_t>(i) * V2_ENTRY_BYTES;
        SavedLinkPosition& e = decoded[i];
        e.spineIndex = readU16(p);
        e.pageNumber = readU16(p + 2);
        e.totalPages = readU16(p + 4);
        const uint8_t flags = p[6];
        if ((flags & ~ALL_FLAGS) != 0) return false;
        e.hasVisibleTextOffset = (flags & FLAG_VISIBLE_OFFSET) != 0;
        e.hasParagraphIndex = (flags & FLAG_PARAGRAPH_INDEX) != 0;
        e.anchorsSync = (flags & FLAG_ANCHORS_SYNC) != 0;
        e.visibleTextOffset = e.hasVisibleTextOffset ? readU32(p + 7) : 0;
        e.paragraphIndex = e.hasParagraphIndex ? readU16(p + 11) : 0;
        if (static_cast<int>(e.spineIndex) >= spineCount) return false;
      }
      for (int i = 0; i < depth; ++i) entries[i] = decoded[i];
      count = depth;
      return true;
    }

    // Legacy v1: [depth] followed by depth x (spine u16 LE, page u16 LE).
    const int depth = data[0];
    if (depth < 1 || depth > MAX_DEPTH || size != 1 + static_cast<size_t>(depth) * V1_ENTRY_BYTES) {
      return false;
    }
    SavedLinkPosition decoded[MAX_DEPTH] = {};
    for (int i = 0; i < depth; ++i) {
      const uint8_t* p = data + 1 + static_cast<size_t>(i) * V1_ENTRY_BYTES;
      decoded[i].spineIndex = readU16(p);
      decoded[i].pageNumber = readU16(p + 2);
      decoded[i].anchorsSync = true;
      if (static_cast<int>(decoded[i].spineIndex) >= spineCount) return false;
    }
    for (int i = 0; i < depth; ++i) entries[i] = decoded[i];
    count = depth;
    if (wasLegacyV1) *wasLegacyV1 = true;
    return true;
  }

 private:
  static constexpr uint8_t FLAG_VISIBLE_OFFSET = 0x01;
  static constexpr uint8_t FLAG_PARAGRAPH_INDEX = 0x02;
  static constexpr uint8_t FLAG_ANCHORS_SYNC = 0x04;
  static constexpr uint8_t ALL_FLAGS = FLAG_VISIBLE_OFFSET | FLAG_PARAGRAPH_INDEX | FLAG_ANCHORS_SYNC;

  int firstSyncAnchorIndex() const {
    for (int i = 0; i < count; ++i) {
      if (entries[i].anchorsSync) return i;
    }
    return -1;
  }

  static void writeU16(uint8_t* p, const uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  }

  static void writeU32(uint8_t* p, const uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
  }

  static uint16_t readU16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
  }

  static uint32_t readU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
  }

  SavedLinkPosition entries[MAX_DEPTH] = {};
  int count = 0;
};
