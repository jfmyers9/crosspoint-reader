#pragma once
#include <cstddef>
#include <string>

// Password extraction is linked with PersistableStore but unused by bookmarks.
namespace obfuscation {
inline std::string deobfuscateFromBase64(const char*, size_t, bool* ok, bool* tooLong) {
  *ok = false;
  *tooLong = false;
  return {};
}
}  // namespace obfuscation
