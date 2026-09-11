#pragma once
#include <Epub.h>

namespace serialization {
inline void writeString(HalFile&, const std::string&) { ++cacheAccesses; }
inline void readString(HalFile&, std::string&) { ++cacheAccesses; }
}  // namespace serialization
