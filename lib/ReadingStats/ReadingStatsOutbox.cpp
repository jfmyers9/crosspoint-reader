#include "ReadingStatsOutbox.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

namespace {
constexpr size_t HEADER_SIZE = 54;
constexpr size_t EVENT_SIZE = 28;
constexpr uint8_t MAGIC[] = {'R', 'S', 'T', 'A'};

bool fail(const char* reason) {
  LOG_ERR("STATS", "Outbox: %s", reason);
  return false;
}

bool isHex(const char* value, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return value[length] == '\0';
}

uint32_t crcBytes(uint32_t crc, const uint8_t* bytes, size_t count) {
  while (count--) {
    crc ^= *bytes++;
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return crc;
}

void put(uint8_t* bytes, uint64_t value, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    bytes[i] = static_cast<uint8_t>(value);
    value >>= 8;
  }
}

uint64_t get(const uint8_t* bytes, size_t count) {
  uint64_t value = 0;
  for (size_t i = 0; i < count; ++i) value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
  return value;
}

bool validEvent(const ReadingStatsEvent& event) {
  return event.startEpochSeconds >= 0 && event.totalPages > 0 && event.page <= event.totalPages &&
         event.durationSeconds > 0;
}

void encodeEvent(uint8_t* bytes, const ReadingStatsEvent& event) {
  put(bytes, event.startMonotonicMs, 8);
  put(bytes + 8, static_cast<uint64_t>(event.startEpochSeconds), 8);
  put(bytes + 16, event.page, 4);
  put(bytes + 20, event.totalPages, 4);
  put(bytes + 24, event.durationSeconds, 4);
}

bool readBatch(const char* path, ReadingStatsOutbox::Batch* output, const ReadingStatsOutbox::Batch* expected) {
  HalFile file;
  if (!Storage.openFileForRead("STATS", path, file)) return fail("cannot open batch");
  uint8_t bytes[HEADER_SIZE];
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes) || std::memcmp(bytes, MAGIC, sizeof(MAGIC)) != 0 ||
      bytes[4] != 1 || bytes[5] == 0 || bytes[5] > ReadingStatsOutbox::MAX_EVENTS) {
    return fail("invalid batch header");
  }
  const uint8_t count = bytes[5];
  if (file.size() != HEADER_SIZE + count * EVENT_SIZE + 4) return fail("incomplete batch");
  char bookHash[33]{};
  char bootId[17]{};
  std::memcpy(bookHash, bytes + 6, 32);
  std::memcpy(bootId, bytes + 38, 16);
  if (!isHex(bookHash, 32) || !isHex(bootId, 16)) return fail("invalid batch identity");
  if (expected && (count != expected->count || std::strcmp(bookHash, expected->bookHash) != 0 ||
                   std::strcmp(bootId, expected->bootId) != 0)) {
    return fail("batch readback mismatch");
  }
  if (output) {
    std::memcpy(output->bookHash, bookHash, sizeof(bookHash));
    std::memcpy(output->bootId, bootId, sizeof(bootId));
    output->count = count;
  }
  uint32_t crc = crcBytes(0xffffffffU, bytes, sizeof(bytes));
  for (uint8_t i = 0; i < count; ++i) {
    if (file.read(bytes, EVENT_SIZE) != EVENT_SIZE) return fail("incomplete event");
    crc = crcBytes(crc, bytes, EVENT_SIZE);
    const uint64_t epoch = get(bytes + 8, 8);
    if (epoch > INT64_MAX) return fail("invalid event time");
    const ReadingStatsEvent event{get(bytes, 8), static_cast<int64_t>(epoch), static_cast<uint32_t>(get(bytes + 16, 4)),
                                  static_cast<uint32_t>(get(bytes + 20, 4)), static_cast<uint32_t>(get(bytes + 24, 4))};
    if (!validEvent(event)) return fail("invalid event");
    if (expected) {
      encodeEvent(bytes, expected->events[i]);
      uint8_t actual[EVENT_SIZE];
      encodeEvent(actual, event);
      if (std::memcmp(bytes, actual, EVENT_SIZE) != 0) return fail("event readback mismatch");
    }
    if (output) output->events[i] = event;
  }
  if (file.read(bytes, 4) != 4 || get(bytes, 4) != (crc ^ 0xffffffffU)) return fail("batch checksum mismatch");
  return true;
}

bool writeBatch(const char* path, const ReadingStatsOutbox::Batch& batch) {
  HalFile file;
  if (!Storage.openFileForWrite("STATS", path, file)) return fail("cannot create batch");
  uint8_t bytes[HEADER_SIZE];
  std::memcpy(bytes, MAGIC, sizeof(MAGIC));
  bytes[4] = 1;
  bytes[5] = batch.count;
  std::memcpy(bytes + 6, batch.bookHash, 32);
  std::memcpy(bytes + 38, batch.bootId, 16);
  uint32_t crc = crcBytes(0xffffffffU, bytes, sizeof(bytes));
  if (file.write(bytes, sizeof(bytes)) != sizeof(bytes)) return fail("header write failed");
  for (uint8_t i = 0; i < batch.count; ++i) {
    encodeEvent(bytes, batch.events[i]);
    crc = crcBytes(crc, bytes, EVENT_SIZE);
    if (file.write(bytes, EVENT_SIZE) != EVENT_SIZE) return fail("event write failed");
  }
  put(bytes, crc ^ 0xffffffffU, 4);
  if (file.write(bytes, 4) != 4) return fail("checksum write failed");
  file.flush();
  return true;
}
}  // namespace

bool ReadingStatsOutbox::save(const char* scopeDir, const Batch& batch) {
  if (!scopeDir || !isHex(batch.bookHash, 32) || !isHex(batch.bootId, 16) || batch.count == 0 ||
      batch.count > MAX_EVENTS) {
    return fail("invalid batch");
  }
  for (uint8_t i = 0; i < batch.count; ++i) {
    if (!validEvent(batch.events[i])) return fail("invalid event");
  }
  static uint32_t sequence = 0;
  // Saves are serialized by the caller; keep path scratch off the reader task's stack.
  static char temporary[128];
  static char committed[128];
  for (int attempt = 0; attempt < 16; ++attempt) {
    const int length = snprintf(temporary, sizeof(temporary), "%s/%s-%08lx-%08lx.tmp", scopeDir, batch.bootId,
                                static_cast<unsigned long>(millis()), static_cast<unsigned long>(sequence++));
    if (length < 0 || static_cast<size_t>(length) >= sizeof(temporary)) return fail("batch path too long");
    std::memcpy(committed, temporary, length + 1);
    std::memcpy(committed + length - 3, "bin", 3);
    if (Storage.exists(temporary) || Storage.exists(committed)) continue;
    if (!writeBatch(temporary, batch) || !readBatch(temporary, nullptr, &batch)) return false;
    if (!Storage.rename(temporary, committed)) return fail("cannot commit batch");
    return true;
  }
  return fail("cannot find unique batch name");
}

bool ReadingStatsOutbox::load(const char* path, Batch& batch) { return readBatch(path, &batch, nullptr); }
