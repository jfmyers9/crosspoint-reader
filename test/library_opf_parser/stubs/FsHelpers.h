#pragma once
#include <string>

// Path transformation belongs to FsHelpers' own suite; fixtures use plain relative paths.
namespace FsHelpers {
inline std::string normalisePath(const std::string& path) { return path; }
inline std::string decodeUriEscapes(const std::string& path) { return path; }
}  // namespace FsHelpers
