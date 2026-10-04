#include "BookOrbitHighlightReader.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "../../src/util/BookmarkUtil.h"

namespace {
constexpr size_t MAX_ENTRY_BYTES = 16384;
constexpr size_t MAX_DOCUMENT_BYTES = 16384;
constexpr size_t MAX_FILE_BYTES = 16 * 1024 * 1024;

// ArduinoJson's normal allocator is unbounded. Account for allocator headers as well
// as JSON storage, and fail before allocating when an entry exceeds the budget.
class LimitedAllocator final : public ArduinoJson::Allocator {
  struct alignas(std::max_align_t) Header {
    size_t size;
  };
  size_t used = 0;

 public:
  void* allocate(size_t size) override { return reallocate(nullptr, size); }
  void deallocate(void* pointer) override {
    if (!pointer) return;
    auto* header = static_cast<Header*>(pointer) - 1;
    used -= header->size;
    std::free(header);
  }
  void* reallocate(void* pointer, size_t size) override {
    if (!size) {
      deallocate(pointer);
      return nullptr;
    }
    auto* old = pointer ? static_cast<Header*>(pointer) - 1 : nullptr;
    const size_t previous = old ? old->size : 0;
    if (size > MAX_DOCUMENT_BYTES - sizeof(Header) || size + sizeof(Header) > MAX_DOCUMENT_BYTES - (used - previous)) {
      return nullptr;
    }
    const size_t total = size + sizeof(Header);
    auto* header = static_cast<Header*>(std::realloc(old, total));
    if (!header) return nullptr;
    header->size = total;
    used = used - previous + total;
    return header + 1;
  }
};

bool whitespace(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

class Input {
  HalFile& file;
  size_t remaining;
  int pending = -1;
  uint8_t buffer[96];
  size_t cursor = 0;
  size_t buffered = 0;

 public:
  bool failed = false;
  Input(HalFile& file, size_t size) : file(file), remaining(size) {}
  int read() {
    if (pending >= 0) {
      const int c = pending;
      pending = -1;
      return c;
    }
    if (!remaining) return -1;
    if (cursor == buffered) {
      const size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
      const int got = file.read(buffer, wanted);
      if (got <= 0 || static_cast<size_t>(got) > wanted) {
        failed = true;
        return -1;
      }
      buffered = static_cast<size_t>(got);
      cursor = 0;
    }
    --remaining;
    return buffer[cursor++];
  }
  int token() {
    int c;
    do {
      c = read();
    } while (whitespace(c));
    return c;
  }
  void putBack(int c) { pending = c; }
};

class EntryInput {
  Input& input;
  size_t count = 0;
  enum class State { Start, FirstKey, Key, Colon, Value, After, Done };
  State state = State::Start;
  bool quoted = false;
  bool escaped = false;
  bool keyString = false;
  bool scalar = false;
  char literal[32] = {};
  size_t literalLength = 0;
  size_t keys = 0;

  bool validLiteral() const {
    if (std::strcmp(literal, "true") == 0 || std::strcmp(literal, "false") == 0 || std::strcmp(literal, "null") == 0)
      return true;
    const char* p = literal;
    if (*p == '-') ++p;
    if (*p == '0')
      ++p;
    else {
      if (*p < '1' || *p > '9') return false;
      do {
        ++p;
      } while (*p >= '0' && *p <= '9');
    }
    if (*p == '.') {
      ++p;
      if (*p < '0' || *p > '9') return false;
      do {
        ++p;
      } while (*p >= '0' && *p <= '9');
    }
    if (*p == 'e' || *p == 'E') {
      ++p;
      if (*p == '+' || *p == '-') ++p;
      if (*p < '0' || *p > '9') return false;
      do {
        ++p;
      } while (*p >= '0' && *p <= '9');
    }
    return *p == '\0';
  }

  // BookmarkFile writes a flat object of scalar fields. Reject ArduinoJson's
  // permissive non-JSON extensions rather than accepting a corrupt source.
  bool accept(int c) {
    if (quoted) {
      if (c < 0x20) return false;
      if (escaped) {
        escaped = false;
        return std::strchr("\"\\/bfnrtu", c) != nullptr;
      }
      if (c == '\\')
        escaped = true;
      else if (c == '"') {
        quoted = false;
        if (keyString) ++keys;
        state = keyString ? State::Colon : State::After;
      }
      return true;
    }
    if (scalar) {
      if (!whitespace(c) && c != ',' && c != '}') {
        if (literalLength == sizeof(literal) - 1) return false;
        literal[literalLength++] = static_cast<char>(c);
        return true;
      }
      if (!validLiteral()) return false;
      scalar = false;
      state = State::After;
    }
    if (whitespace(c)) return true;
    switch (state) {
      case State::Start:
        state = State::FirstKey;
        return c == '{';
      case State::FirstKey:
        if (c == '}') {
          state = State::Done;
          return true;
        }
        [[fallthrough]];
      case State::Key:
        if (c != '"') return false;
        quoted = keyString = true;
        return true;
      case State::Colon:
        state = State::Value;
        return c == ':';
      case State::Value:
        if (c == '"') {
          quoted = true;
          keyString = false;
          return true;
        }
        if (c != '-' && (c < '0' || c > '9') && c != 't' && c != 'f' && c != 'n') return false;
        scalar = true;
        literalLength = 1;
        std::memset(literal, 0, sizeof(literal));
        literal[0] = static_cast<char>(c);
        return true;
      case State::After:
        if (c == ',') {
          state = State::Key;
          return true;
        }
        if (c == '}') {
          state = State::Done;
          return true;
        }
        return false;
      case State::Done:
        return false;
    }
    return false;
  }

 public:
  explicit EntryInput(Input& input) : input(input) {}
  int read() {
    if (count++ >= MAX_ENTRY_BYTES) return -1;
    const int c = input.read();
    return c >= 0 && accept(c) ? c : -1;
  }
  bool complete() const { return state == State::Done; }
  size_t keyCount() const { return keys; }
  size_t readBytes(char* destination, size_t length) {
    size_t copied = 0;
    for (; copied < length; ++copied) {
      const int c = read();
      if (c < 0) break;
      destination[copied] = static_cast<char>(c);
    }
    return copied;
  }
};

bool visitHighlight(JsonObjectConst obj, bool (*visitor)(void*, const BookmarkEntry&), void* context) {
  // A present end marker is a highlight claim; do not silently skip invalid ranges.
  if (obj["he"].isUnbound()) return true;
  if (!obj["he"].is<uint32_t>()) return false;
  if (obj["he"].as<uint32_t>() == 0) return true;
  if (!obj["si"].is<uint16_t>() || !obj["vo"].is<uint32_t>() || obj["he"].as<uint32_t>() <= obj["vo"].as<uint32_t>() ||
      !obj["summary"].is<JsonString>())
    return false;
  const JsonString summary = obj["summary"].as<JsonString>();
  if (summary.size() == 0 || summary.size() > BookmarkEntry::MAX_HIGHLIGHT_QUOTE_LENGTH ||
      std::strlen(summary.c_str()) != summary.size())
    return false;
  if (!obj["name"].isUnbound() &&
      (!obj["name"].is<JsonString>() || obj["name"].as<JsonString>().size() > BookmarkEntry::MAX_NAME_LENGTH ||
       std::strlen(obj["name"].as<const char*>()) != obj["name"].as<JsonString>().size()))
    return false;
  if (!visitor) return true;
  // Only two bounded strings are copied; no full collection is retained.
  BookmarkEntry bookmark{};
  bookmark.computedSpineIndex = obj["si"].as<uint16_t>();
  bookmark.visibleTextOffset = obj["vo"].as<uint32_t>();
  bookmark.hasVisibleTextOffset = true;
  bookmark.highlightEndOffset = obj["he"].as<uint32_t>();
  bookmark.summary.assign(summary.c_str(), summary.size());
  bookmark.name = obj["name"] | "";
  return visitor(context, bookmark);
}

bool visitEntry(Input& input, bool (*visitor)(void*, const BookmarkEntry&), void* context) {
  LimitedAllocator allocator;
  JsonDocument doc(&allocator);
  EntryInput entryInput(input);
  if (deserializeJson(doc, entryInput, DeserializationOption::NestingLimit(8)) || !entryInput.complete() ||
      !doc.is<JsonObject>() || doc.size() != entryInput.keyCount())
    return false;
  return visitHighlight(doc.as<JsonObjectConst>(), visitor, context);
}
}  // namespace

bool BookOrbitHighlightReader::visit(const std::string& bookPath, bool (*visitor)(void*, const BookmarkEntry&),
                                     void* context) {
  std::string path = BookmarkUtil::getBookmarkPath(bookPath);
  if (!Storage.exists(path.c_str())) {
    path += ".bak";
    if (!Storage.exists(path.c_str())) return true;
  }
  HalFile file;
  if (!Storage.openFileForRead("BOHL", path.c_str(), file) || file.isDirectory()) return false;
  const size_t size = file.size();
  if (size == 0 || size > MAX_FILE_BYTES) return false;
  Input input(file, size);
  if (input.token() != '{' || input.token() != '"') return false;
  for (const char* key = "bookmarks\""; *key; ++key) {
    if (input.read() != *key) return false;
  }
  if (input.token() != ':' || input.token() != '[') return false;
  int next = input.token();
  if (next != ']') {
    while (true) {
      if (next != '{') return false;
      input.putBack(next);
      if (!visitEntry(input, visitor, context)) return false;
      next = input.token();
      if (next == ']') break;
      if (next != ',') return false;
      next = input.token();
    }
  }
  return input.token() == '}' && input.token() == -1 && !input.failed;
}
