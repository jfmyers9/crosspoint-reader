#pragma once
#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace freeink {
inline std::string fetchOrigin(const std::string& url) {
  const auto scheme = url.find("://");
  return scheme == std::string::npos ? std::string() : url.substr(0, url.find('/', scheme + 3));
}
struct SecureHttpClient {
  bool begin(const std::string&) { return true; }
  int getStatus() const { return 200; }
  std::string getHeader(const std::string&) const { return {}; }
  bool hasContentLength() const { return true; }
  size_t getContentLength() const { return 4; }
  bool aborted() const { return false; }
  static bool resolveUrl(const std::string& base, const std::string& ref, std::string& out) {
    out = ref.find("://") != std::string::npos ? ref : fetchOrigin(base) + ref;
    return !ref.empty();
  }
  int GET(const std::function<bool(const uint8_t*, size_t)>& onData) {
    const uint8_t data[] = {'b', 'o', 'o', 'k'};
    onData(data, sizeof(data));
    return 200;
  }
  int sendRequest(const char*, const uint8_t*, size_t, const std::function<bool(const uint8_t*, size_t)>& onData,
                  const std::function<bool()>& = nullptr) {
    return GET(onData);
  }
  void setTimeout(int) {}
  void setInsecure() {}
  void setUserAgent(const char*) {}
  void setBasicAuth(const std::string&, const std::string&) {}
  void addHeader(const std::string&, const std::string&) {}
};
struct FetchOptions {
  bool redirectToHttp = false;
};
struct FetchSink {
  std::function<bool(const uint8_t*, size_t)> write;
  std::function<bool()> rewind;
  std::function<void(size_t, size_t)> progress;
};
struct FetchResult {
  int status = 0;
  size_t bytes = 0, total = 0;
  bool complete = false, stopped = false, aborted = false;
};
struct Attempt {
  bool sameOrigin;
  uint32_t freeHeap, maxAlloc;
};
inline std::vector<Attempt> attempts;
inline int connections = 0;

// Model the SDK order: configure after begin, then abort check before connect.
template <typename Configure, typename Abort>
FetchResult fetchResumable(const std::string&, const FetchOptions&, Configure configure, const FetchSink& sink,
                           Abort abort) {
  FetchResult result;
  for (const auto& attempt : attempts) {
    SecureHttpClient http;
    ESP.freeHeap = attempt.freeHeap;
    ESP.maxAlloc = attempt.maxAlloc;
    configure(http, attempt.sameOrigin);
    if (abort()) {
      result.aborted = true;
      return result;
    }
    ++connections;
  }
  const uint8_t data[] = {'b', 'o', 'o', 'k'};
  result.status = 200;
  result.complete = sink.write(data, sizeof(data));
  result.stopped = !result.complete;
  result.bytes = result.total = result.complete ? sizeof(data) : 0;
  return result;
}
}  // namespace freeink
