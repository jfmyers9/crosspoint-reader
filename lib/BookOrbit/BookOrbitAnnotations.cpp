#include "BookOrbitAnnotations.h"

#if defined(CROSSPOINT_ENABLE_BOOKORBIT_STATS)
#include <ChapterXPathResolver.h>
#include <Epub.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <KOReaderCredentialStore.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <esp_timer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <new>

#include "BookOrbitAnnotationsClient.h"
#include "BookOrbitHighlightReader.h"
#include "BookOrbitStatsClient.h"

namespace {
constexpr char ROOT[] = "/.crosspoint/bookorbit-highlights";
constexpr char API_SUFFIX[] = "/api/v1/koreader";
constexpr size_t MAX_REQUESTS = 32;
constexpr uint64_t BUDGET_MS = 45000;
constexpr size_t MAX_STATE_BYTES = 160;

bool validEpoch(int64_t value) { return value >= 1704067200LL && value < 4102444800LL; }

bool validHash(const std::string& value) {
  return value.size() == 32 && value.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::string digest(const std::string& value) {
  MD5Builder md5;
  md5.begin();
  md5.add(value.c_str());
  md5.calculate();
  return md5.toString().c_str();
}

// Small sidecars are bounded before allocation. A missing primary may have a
// recoverable backup; a corrupt primary is never silently assigned a new identity.
bool readState(const std::string& path, std::string& value) {
  value.clear();
  const std::string source = Storage.exists(path.c_str()) ? path : path + ".bak";
  if (!Storage.exists(source.c_str())) return true;
  auto file = Storage.open(source.c_str());
  if (!file || file.isDirectory() || file.fileSize() == 0 || file.fileSize() >= MAX_STATE_BYTES) return false;
  char bytes[MAX_STATE_BYTES];
  const size_t size = file.fileSize();
  if (file.read(bytes, size) != static_cast<int>(size)) return false;
  value.assign(bytes, size);
  return value.find('\0') == std::string::npos;
}

bool saveState(const std::string& path, const std::string& value) {
  const std::string temporary = path + ".tmp";
  const std::string backup = path + ".bak";
  if (value.empty() || value.size() >= MAX_STATE_BYTES) return false;
  {
    auto file = Storage.open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!file || file.write(value.data(), value.size()) != value.size()) return false;
    file.flush();
  }
  std::string check;
  if (!readState(temporary, check) || check != value) return false;
  if (Storage.exists(path.c_str())) {
    if (Storage.exists(backup.c_str()) && !Storage.remove(backup.c_str())) return false;
    if (!Storage.rename(path.c_str(), backup.c_str())) return false;
  }
  if (!Storage.rename(temporary.c_str(), path.c_str())) {
    if (Storage.exists(backup.c_str())) Storage.rename(backup.c_str(), path.c_str());
    return false;
  }
  if (Storage.exists(backup.c_str())) Storage.remove(backup.c_str());
  return true;
}

struct ExportState {
  int64_t created = 0;
  int64_t updated = 0;
  std::string pending;
  std::string acknowledged;

  bool load(const std::string& path) {
    std::string value;
    if (!readState(path, value)) return false;
    if (value.empty()) return true;
    long long first = 0, last = 0;
    char wanted[33], ack[33], trailing;
    if (sscanf(value.c_str(), "BOH1 %lld %lld %32s %32s %c", &first, &last, wanted, ack, &trailing) != 4 ||
        !validEpoch(first) || !validEpoch(last) || last < first || !validHash(wanted) ||
        (strcmp(ack, "-") != 0 && !validHash(ack)))
      return false;
    created = first;
    updated = last;
    pending = wanted;
    acknowledged = ack;
    return true;
  }

  bool save(const std::string& path) const {
    char value[MAX_STATE_BYTES];
    snprintf(value, sizeof(value), "BOH1 %lld %lld %s %s\n", static_cast<long long>(created),
             static_cast<long long>(updated), pending.c_str(), acknowledged.empty() ? "-" : acknowledged.c_str());
    return saveState(path, value);
  }
};

bool nextTimestamp(const std::string& folder, int64_t& timestamp) {
  int64_t now;
  if (!halClock.getUtcTime(now) || !validEpoch(now)) return false;
  const std::string path = folder + "/clock";
  std::string value;
  if (!readState(path, value)) return false;
  long long previous = 0;
  char trailing;
  if (!value.empty() && (sscanf(value.c_str(), "BOHC1 %lld %c", &previous, &trailing) != 1 || !validEpoch(previous)))
    return false;
  // BookOrbit reconciles by datetime as well as position. Reserve distinct,
  // persistent identity seconds before sending, even for a same-second import.
  timestamp = std::max(now, static_cast<int64_t>(previous + 1));
  if (!validEpoch(timestamp)) return false;
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "BOHC1 %lld\n", static_cast<long long>(timestamp));
  return saveState(path, buffer);
}

bool formatTimestamp(int64_t timestamp, char (&buffer)[20]) {
  const time_t epoch = static_cast<time_t>(timestamp);
  struct tm utc{};
  return gmtime_r(&epoch, &utc) && strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &utc) == 19;
}

struct SyncContext {
  const std::string& bookPath;
  std::string bookHash;
  std::string folder;
  uint64_t startedMs;
  size_t requests = 0;
  BookOrbitAnnotations::Result result = BookOrbitAnnotations::Result::Complete;

  static bool upload(void* data, const BookmarkEntry& entry) {
    auto& self = *static_cast<SyncContext*>(data);
    char anchor[64];
    snprintf(anchor, sizeof(anchor), "%u:%u:%u", static_cast<unsigned>(entry.computedSpineIndex),
             static_cast<unsigned>(entry.visibleTextOffset), static_cast<unsigned>(entry.highlightEndOffset));
    const std::string path = self.folder + "/" + digest(anchor) + ".state";
    // Length-prefix mutable fields so embedded delimiters cannot alias revisions.
    const std::string revision = digest(std::string("1:") + anchor + ":" + std::to_string(entry.summary.size()) + ":" +
                                        entry.summary + ":" + entry.name);
    ExportState state;
    if (!state.load(path)) {
      self.result = BookOrbitAnnotations::Result::Failed;
      return true;  // One corrupt sidecar must not starve unrelated highlights.
    }
    if (state.pending == revision && state.acknowledged == revision) return true;
    if (self.requests >= MAX_REQUESTS ||
        (self.requests > 0 && static_cast<uint64_t>(esp_timer_get_time()) / 1000 - self.startedMs >= BUDGET_MS)) {
      self.result = BookOrbitAnnotations::Result::Pending;
      return false;
    }

    std::string start, end;
    {
      // Metadata and parser allocations exist only during conversion and are
      // released before the HTTP/TLS request. Never hold the whole bookmark list.
      const auto epub = std::unique_ptr<Epub>(new (std::nothrow) Epub(self.bookPath, "/.crosspoint"));
      if (!epub) return false;
      epub->setupCacheDir();
      // The synchronous resolver borrows this view; aliasing an empty owner
      // avoids a fallible shared_ptr control-block allocation.
      const std::shared_ptr<Epub> view(std::shared_ptr<Epub>{}, epub.get());
      if (!epub->load(false, true) || !ChapterXPathResolver::findXPathRangeForVisibleTextOffsets(
                                          view, entry.computedSpineIndex, entry.visibleTextOffset,
                                          entry.highlightEndOffset, start, end, entry.summary)) {
        self.result = BookOrbitAnnotations::Result::Failed;
        return true;  // Unsupported positions stay local; other highlights can still export.
      }
    }
    if (state.pending != revision) {
      if (!nextTimestamp(self.folder, state.updated)) return false;
      if (state.created == 0) state.created = state.updated;
      state.pending = revision;
      if (!state.save(path)) return false;
    }
    char created[20], updated[20];
    if (!formatTimestamp(state.created, created) || !formatTimestamp(state.updated, updated)) return false;
    ++self.requests;
    if (!BookOrbitAnnotationsClient::upload(self.bookHash.c_str(), created, updated, start.c_str(), end.c_str(),
                                            entry.summary.c_str(), entry.name.c_str())) {
      if (BookOrbitAnnotationsClient::lastUploadWasUnmatched()) self.result = BookOrbitAnnotations::Result::Unmatched;
      return false;
    }
    state.acknowledged = revision;
    return state.save(path);
  }
};
}  // namespace

BookOrbitAnnotations::Result BookOrbitAnnotations::sync(const std::string& bookPath) {
  const auto& url = KOREADER_STORE.getBaseUrl();
  if (!KOREADER_STORE.hasCredentials() || url.size() < sizeof(API_SUFFIX) - 1 ||
      url.compare(url.size() - (sizeof(API_SUFFIX) - 1), sizeof(API_SUFFIX) - 1, API_SUFFIX) != 0)
    return Result::Skipped;
  size_t highlights = 0;
  if (!BookOrbitHighlightReader::visit(
          bookPath,
          [](void* count, const BookmarkEntry&) {
            ++*static_cast<size_t*>(count);
            return true;
          },
          &highlights)) {
    LOG_ERR("BOHL", "Cannot read highlights; nothing uploaded");
    return Result::Failed;
  }
  if (highlights == 0) return Result::Skipped;
  const char* device = BookOrbitStatsClient::deviceId();
  const std::string bookHash = KOReaderDocumentId::calculate(bookPath);
  if (!device[0] || !validHash(bookHash)) return Result::Failed;
  const std::string scope = digest(url + "\n" + KOREADER_STORE.getUsername() + "\n" + device);
  SyncContext context{bookPath, bookHash, std::string(ROOT) + "/" + scope + "/" + bookHash,
                      static_cast<uint64_t>(esp_timer_get_time()) / 1000};
  if (!Storage.ensureDirectoryExists(context.folder.c_str())) return Result::Failed;
  if (!BookOrbitHighlightReader::visit(bookPath, SyncContext::upload, &context)) {
    if (context.result == Result::Complete) context.result = Result::Failed;
    LOG_ERR("BOHL", "Highlight export incomplete; local highlights retained");
  }
  return context.result;
}
#endif
