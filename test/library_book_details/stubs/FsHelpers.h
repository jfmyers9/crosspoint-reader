#pragma once
#include <string>
namespace FsHelpers {
inline bool hasEpubExtension(const std::string& path) { return path.ends_with(".epub"); }
inline bool hasJpgExtension(const std::string& path) { return path.ends_with(".jpg"); }
inline bool hasPngExtension(const std::string& path) { return path.ends_with(".png"); }
}  // namespace FsHelpers
