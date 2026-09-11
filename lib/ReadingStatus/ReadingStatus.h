#pragma once

#include <cstdint>
#include <string>

namespace ReadingStatus {
enum class State : uint8_t { Unknown, Unread, Reading, Finished };
struct Status {
  State state = State::Unknown;
  uint8_t percent = 0;
};

Status load(const std::string& path);
bool mark(const std::string& path, State state);
// Fraction is in [0, 1]. Finished remains sticky until explicitly marked unread.
bool update(const std::string& path, float fraction, bool finished = false);
bool move(const std::string& oldPath, const std::string& newPath);
}  // namespace ReadingStatus
