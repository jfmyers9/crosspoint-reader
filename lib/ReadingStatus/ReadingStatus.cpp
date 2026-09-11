#include "ReadingStatus.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr char DIRECTORY[] = "/.crosspoint/reading-status";
constexpr size_t FILE_PATH_SIZE = 64;

void recordPath(const std::string& path, char* output, const char* suffix) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char c : path) {
    hash = (hash ^ c) * UINT64_C(1099511628211);
  }
  snprintf(output, FILE_PATH_SIZE, "%s/%016llx%s", DIRECTORY, static_cast<unsigned long long>(hash), suffix);
}

bool readRecord(const char* filename, const std::string& path, ReadingStatus::Status& status) {
  HalFile file;
  if (!Storage.exists(filename) || !Storage.openFileForRead("RDS", filename, file)) return false;
  uint8_t header[8];
  if (file.read(header, sizeof(header)) != sizeof(header) || memcmp(header, "RS\1", 3) != 0 || header[5] != 0 ||
      header[3] < static_cast<uint8_t>(ReadingStatus::State::Unread) ||
      header[3] > static_cast<uint8_t>(ReadingStatus::State::Finished) || header[4] > 100 ||
      (header[3] == static_cast<uint8_t>(ReadingStatus::State::Unread) && header[4] != 0) ||
      (header[3] == static_cast<uint8_t>(ReadingStatus::State::Finished) && header[4] != 100) ||
      (header[3] == static_cast<uint8_t>(ReadingStatus::State::Reading) && header[4] >= 100) ||
      (header[6] | (static_cast<size_t>(header[7]) << 8)) != path.size() ||
      file.fileSize64() != sizeof(header) + path.size()) return false;
  char chunk[64];
  for (size_t offset = 0; offset < path.size();) {
    const size_t count = std::min(sizeof(chunk), path.size() - offset);
    if (file.read(chunk, count) != static_cast<int>(count) || memcmp(chunk, path.data() + offset, count) != 0)
      return false;
    offset += count;
  }
  status = {static_cast<ReadingStatus::State>(header[3]), header[4]};
  return true;
}

bool save(const std::string& path, const ReadingStatus::Status status) {
  if (path.empty() || path.size() > UINT16_MAX) {
    LOG_ERR("RDS", "Invalid source path length");
    return false;
  }
  const auto previous = ReadingStatus::load(path);
  if (previous.state == status.state && previous.percent == status.percent) return true;
  char finalPath[FILE_PATH_SIZE];
  char temporaryPath[FILE_PATH_SIZE];
  char backupPath[FILE_PATH_SIZE];
  recordPath(path, finalPath, ".bin");
  recordPath(path, temporaryPath, ".tmp");
  recordPath(path, backupPath, ".bak");
  if (!Storage.ensureDirectoryExists(DIRECTORY)) {
    LOG_ERR("RDS", "Could not create status directory");
    return false;
  }
  {
    HalFile file;
    const uint8_t header[] = {'R', 'S', 1, static_cast<uint8_t>(status.state), status.percent, 0,
                              static_cast<uint8_t>(path.size()), static_cast<uint8_t>(path.size() >> 8)};
    if (!Storage.openFileForWrite("RDS", temporaryPath, file) || file.write(header, sizeof(header)) != sizeof(header) ||
        file.write(path.data(), path.size()) != path.size()) {
      LOG_ERR("RDS", "Failed to write reading status");
      return false;
    }
    file.flush();
  }
  // Keep the previous complete record recoverable across FAT's separate rename operations.
  if (Storage.exists(finalPath)) {
    ReadingStatus::Status canonical;
    if (readRecord(finalPath, path, canonical)) {
      if ((Storage.exists(backupPath) && !Storage.remove(backupPath)) || !Storage.rename(finalPath, backupPath)) {
        LOG_ERR("RDS", "Failed to preserve previous reading status");
        return false;
      }
    } else if (!Storage.remove(finalPath)) {
      // An invalid canonical record must never replace a recoverable backup.
      LOG_ERR("RDS", "Failed to remove invalid reading status");
      return false;
    }
  }
  if (!Storage.rename(temporaryPath, finalPath)) {
    LOG_ERR("RDS", "Failed to replace reading status");
    return false;
  }
  if (Storage.exists(backupPath)) Storage.remove(backupPath);
  return true;
}
}  // namespace

ReadingStatus::Status ReadingStatus::load(const std::string& path) {
  Status status;
  char filename[FILE_PATH_SIZE];
  recordPath(path, filename, ".bin");
  if (readRecord(filename, path, status)) return status;
  recordPath(path, filename, ".bak");
  readRecord(filename, path, status);
  return status;
}

bool ReadingStatus::mark(const std::string& path, const State state) {
  if (state != State::Unread && state != State::Finished) {
    LOG_ERR("RDS", "Unsupported manual reading status");
    return false;
  }
  return save(path, {state, static_cast<uint8_t>(state == State::Finished ? 100 : 0)});
}

bool ReadingStatus::update(const std::string& path, const float fraction, const bool finished) {
  if (!std::isfinite(fraction) || fraction < 0 || fraction > 1) {
    LOG_ERR("RDS", "Invalid reading fraction");
    return false;
  }
  if (load(path).state == State::Finished) return true;
  return save(path, {finished ? State::Finished : State::Reading,
                     static_cast<uint8_t>(finished ? 100 : std::min(99, static_cast<int>(fraction * 100)))});
}

bool ReadingStatus::move(const std::string& oldPath, const std::string& newPath) {
  if (oldPath == newPath) return true;
  const auto status = load(oldPath);
  if (status.state == State::Unknown) return true;
  if (!save(newPath, status)) return false;
  char filename[FILE_PATH_SIZE];
  recordPath(oldPath, filename, ".bin");
  if (Storage.exists(filename)) Storage.remove(filename);
  recordPath(oldPath, filename, ".bak");
  if (Storage.exists(filename)) Storage.remove(filename);
  return true;
}
