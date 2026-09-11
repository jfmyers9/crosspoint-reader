#pragma once

#include <ArduinoJson.h>

#include <cstdint>

inline uint8_t readLibraryLayout(JsonVariantConst doc, bool& needsResave) {
  constexpr uint8_t COMPACT = 0;
  constexpr uint8_t COVERS = 1;
  const auto current = doc["libraryLayout"];
  const auto legacy = doc["libraryCoverView"];
  // The old libraryView controlled Browse Files, not the indexed Library.
  needsResave |= !legacy.isUnbound() || !doc["libraryView"].isUnbound();
  if (!current.isUnbound()) {
    if (current.is<uint8_t>() && current.as<uint8_t>() <= COVERS) {
      return current.as<uint8_t>();
    }
    needsResave = true;
    return COMPACT;
  }
  if (legacy.is<bool>()) {
    return legacy.as<bool>() ? COVERS : COMPACT;
  }
  if (legacy.is<uint8_t>() && legacy.as<uint8_t>() <= COVERS) {
    return legacy.as<uint8_t>();
  }
  return COMPACT;
}
