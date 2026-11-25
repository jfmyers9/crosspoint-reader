#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "LinkBackStack.h"

// Single owner of `<cachePath>/links.bin` persistence.
// Uses the same temp-file + rename pattern as ProgressFile so an interrupted
// write never leaves a torn record, and persists transitions eagerly so a crash
// or battery loss mid-footnote cannot orphan `progress.bin` at the endnotes.
namespace LinkBackStackFile {

inline void clear(const std::string& cachePath) {
  const std::string finalPath = cachePath + "/links.bin";
  const std::string tmpPath = cachePath + "/links.bin.tmp";
  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
}

inline bool save(const std::string& cachePath, const LinkBackStack& stack) {
  if (stack.empty()) {
    clear(cachePath);
    return true;
  }

  uint8_t data[LinkBackStack::MAX_SERIALIZED_BYTES];
  const size_t len = stack.serialize(data, sizeof(data));
  if (len == 0) return false;

  const std::string finalPath = cachePath + "/links.bin";
  const std::string tmpPath = cachePath + "/links.bin.tmp";

  {
    HalFile f;
    if (!Storage.openFileForWrite("ERS", tmpPath, f)) {
      LOG_ERR("ERS", "Could not open temp link stack for write: %s", tmpPath.c_str());
      return false;
    }
    if (f.write(data, len) != len) {
      LOG_ERR("ERS", "Failed to write temp link stack: %s", tmpPath.c_str());
      return false;
    }
    f.flush();
  }

  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("ERS", "Failed to rename temp link stack into place: %s", finalPath.c_str());
    return false;
  }
  return true;
}

inline bool load(const std::string& cachePath, LinkBackStack& stack, const int spineCount,
                 bool* wasLegacyV1 = nullptr) {
  stack.clear();
  if (wasLegacyV1) *wasLegacyV1 = false;

  const std::string finalPath = cachePath + "/links.bin";
  if (!Storage.exists(finalPath.c_str())) return false;

  uint8_t data[LinkBackStack::MAX_SERIALIZED_BYTES];
  size_t fileSize = 0;
  bool readOk = false;
  {
    HalFile f;
    if (Storage.openFileForRead("ERS", finalPath, f)) {
      fileSize = f.size();
      if (fileSize > 0 && fileSize <= sizeof(data)) {
        readOk = f.read(data, fileSize) == static_cast<int>(fileSize);
      }
    }
  }

  if (readOk && stack.deserialize(data, fileSize, spineCount, wasLegacyV1)) {
    return true;
  }

  // Discard corrupt or oversized records rather than letting them linger.
  clear(cachePath);
  return false;
}

}  // namespace LinkBackStackFile
