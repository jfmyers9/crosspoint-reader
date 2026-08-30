#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <cctype>
#include <ResumableFetch.h>

#include <functional>
#include <string>

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
#include <HTTPClient.h>
#include <TailscaleManager.h>
#include <TailscaleNetworkClient.h>
#endif

#include "WifiPowerSaveGuard.h"

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }

namespace {
// Per-socket-op timeout. Some OPDS download endpoints are slow to send headers
// (>15s) and chunked catalogs stall mid-body, so 15s killed them. 60s gives
// slow servers room.
constexpr int HTTP_TIMEOUT_MS = 60000;

#if defined(CROSSPOINT_ENABLE_TAILSCALE)
constexpr int MAX_REDIRECTS = 5;

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool sameOrigin(const std::string& left, const std::string& right) {
  const auto originLength = [](const std::string& url) {
    const size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return size_t{0};
    const size_t pathStart = url.find('/', schemeEnd + 3);
    return pathStart == std::string::npos ? url.size() : pathStart;
  };
  const size_t leftLength = originLength(left);
  const size_t rightLength = originLength(right);
  if (leftLength == 0 || leftLength != rightLength) return false;
  for (size_t i = 0; i < leftLength; ++i) {
    if (tolower(static_cast<unsigned char>(left[i])) != tolower(static_cast<unsigned char>(right[i]))) return false;
  }
  return true;
}

class HttpSinkStream final : public Stream {
 public:
  HttpSinkStream(const freeink::FetchSink& sink, const bool* cancelFlag, size_t total)
      : sink(sink), cancelFlag(cancelFlag), total(total) {}

  size_t write(uint8_t value) override { return write(&value, 1); }
  size_t write(const uint8_t* data, size_t len) override {
    if (cancelFlag && *cancelFlag) {
      aborted = true;
      return 0;
    }
    if (!sink.write(data, len)) {
      failed = true;
      return 0;
    }
    downloaded += len;
    if (sink.progress && total > 0) sink.progress(downloaded, total);
    return len;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  size_t downloaded = 0;
  bool aborted = false;
  bool failed = false;

 private:
  const freeink::FetchSink& sink;
  const bool* cancelFlag;
  size_t total;
};

HttpDownloader::DownloadError runGetMagicDns(const std::string& url, const std::string& username,
                                             const std::string& password, const std::vector<HttpDownloader::Header>& headers,
                                             const freeink::FetchSink& sink, const bool* cancelFlag, size_t* bytesOut) {
  if (url.compare(0, 7, "http://") != 0) {
    LOG_ERR("HTTP", "Tailnet MagicDNS currently requires plain HTTP: %s", url.c_str());
    return HttpDownloader::HTTP_ERROR;
  }

  // Keep the sizable HTTP transport objects off the activity task stack. Their
  // internal buffers are allocated only for the duration of this request.
  auto transport = makeUniqueNoThrow<TailscaleNetworkClient>();
  auto http = makeUniqueNoThrow<HTTPClient>();
  if (!transport || !http) {
    LOG_ERR("HTTP", "OOM: tailnet HTTP transport");
    return HttpDownloader::HTTP_ERROR;
  }

  WifiPowerSaveGuard psGuard;
  http->setConnectTimeout(HTTP_TIMEOUT_MS);
  http->setTimeout(HTTP_TIMEOUT_MS);
  http->setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
  std::string activeUrl = url;
  bool sendCredentials = true;
  int status = 0;
  for (int hop = 0;; ++hop) {
    http->setAuthorization("");
    if (sendCredentials && !username.empty() && !password.empty()) {
      http->setAuthorization(username.c_str(), password.c_str());
    }
    if (!http->begin(*transport, activeUrl.c_str())) {
      LOG_ERR("HTTP", "Bad tailnet URL: %s", activeUrl.c_str());
      return HttpDownloader::HTTP_ERROR;
    }

    if (sendCredentials) {
      for (const auto& h : headers) http->addHeader(h.first.c_str(), h.second.c_str());
    }
    status = http->GET();
    if (!isRedirect(status)) break;
    if (hop >= MAX_REDIRECTS) {
      LOG_ERR("HTTP", "Too many tailnet redirects");
      http->end();
      return HttpDownloader::HTTP_ERROR;
    }

    const String location = http->getLocation();
    std::string nextUrl;
    if (location.isEmpty() || !freeink::SecureHttpClient::resolveUrl(activeUrl, location.c_str(), nextUrl)) {
      LOG_ERR("HTTP", "Bad tailnet redirect: %d", status);
      http->end();
      return HttpDownloader::HTTP_ERROR;
    }
    if (nextUrl.compare(0, 7, "http://") != 0) {
      LOG_ERR("HTTP", "Tailnet redirect requires unsupported HTTPS transport");
      http->end();
      return HttpDownloader::HTTP_ERROR;
    }
    sendCredentials = sendCredentials && sameOrigin(activeUrl, nextUrl);
    activeUrl = std::move(nextUrl);
    http->end();
  }
  if (status == 401 || status == 403) {
    LOG_ERR("HTTP", "Tailnet request unauthorized: %d", status);
    http->end();
    return HttpDownloader::UNAUTHORIZED;
  }
  if (status != HTTP_CODE_OK) {
    LOG_ERR("HTTP", "Tailnet request failed: %d", status);
    http->end();
    return HttpDownloader::HTTP_ERROR;
  }

  const int contentLength = http->getSize();
  const size_t total = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;
  HttpSinkStream output(sink, cancelFlag, total);
  const int written = http->writeToStream(&output);
  http->end();
  if (bytesOut) *bytesOut = output.downloaded;
  if (output.aborted) return HttpDownloader::ABORTED;
  if (output.failed) return HttpDownloader::FILE_ERROR;
  if (written < 0 || (total > 0 && output.downloaded != total)) {
    LOG_ERR("HTTP", "Incomplete tailnet response: got %zu of %zu bytes", output.downloaded, total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}
#endif

// All HTTP(S) fetches go through wolfSSL (the firmware's only TLS stack: it
// speaks TLS 1.3 and reads large bodies reliably). Plain-http URLs still use a
// WiFiClient here, so this is safe for non-TLS targets too. A body cut short
// mid-transfer resumes with a Range request (see ResumableFetch.h).
HttpDownloader::DownloadError runGetSecure(const std::string& url, const std::string& username,
                                           const std::string& password,
                                           const std::vector<HttpDownloader::Header>& headers,
                                           const freeink::FetchSink& sink, const bool* cancelFlag = nullptr,
                                           size_t* bytesOut = nullptr, const bool downgradeRedirectsToHttp = false) {
#if defined(CROSSPOINT_ENABLE_TAILSCALE)
  if (TAILSCALE.isMagicDnsUrl(url)) return runGetMagicDns(url, username, password, headers, sink, cancelFlag, bytesOut);
  if (!TAILSCALE.prepareUrl(url)) return HttpDownloader::HTTP_ERROR;
#endif
  WifiPowerSaveGuard psGuard;
  freeink::FetchOptions options;
  options.redirectToHttp = downgradeRedirectsToHttp;
  const freeink::FetchResult result = freeink::fetchResumable(
      url, options,
      [&](freeink::SecureHttpClient& http, const bool sameOrigin) {
        http.setTimeout(HTTP_TIMEOUT_MS);
        http.setInsecure();
        // setUserAgent replaces SecureHttpClient's built-in UA; addHeader would
        // append a second User-Agent header, which strict servers reject (aiohttp
        // answers 400 "Duplicate 'User-Agent' header found").
        http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
        // Credentials and caller headers stay with the starting origin; a
        // redirect elsewhere (or to plain http) gets neither.
        if (sameOrigin) {
          if (!username.empty() && !password.empty()) http.setBasicAuth(username, password);
          for (const auto& h : headers) http.addHeader(h.first, h.second);
        }
        LOG_DBG("HTTP", "wolfSSL GET: %s (heap %u, max block %u)", url.c_str(), (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
      },
      sink, [cancelFlag] { return cancelFlag && *cancelFlag; });
  if (bytesOut) *bytesOut = result.bytes;

  if (result.aborted) return HttpDownloader::ABORTED;
  if (result.stopped) return HttpDownloader::FILE_ERROR;
  if (result.status == 401 || result.status == 403) {
    LOG_ERR("HTTP", "wolfSSL request unauthorized: status %d: %s", result.status, url.c_str());
    return HttpDownloader::UNAUTHORIZED;
  }
  if (result.status < 200 || result.status >= 300) {
    LOG_ERR("HTTP", "wolfSSL request failed: status %d: %s", result.status, url.c_str());
    return HttpDownloader::HTTP_ERROR;
  }
  if (!result.complete) {
    LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", result.bytes, result.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}

}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  return fetchUrl(
      url, [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; }, username,
      password);
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  freeink::FetchSink sink;
  sink.write = onData;
  return runGetSecure(url, username, password, {}, sink) == OK;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, const bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             const std::vector<Header>& headers,
                                                             bool downgradeRedirectsToHttp) {
  LOG_DBG("HTTP", "Downloading: %s -> %s", url.c_str(), destPath.c_str());

  // Stage in <dest>.part: a failed or cancelled download never replaces an
  // existing copy, and a partial file never sits under the real name.
  const std::string partPath = destPath + ".part";
  Storage.remove(partPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", partPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  freeink::FetchSink sink;
  sink.write = [&file](const uint8_t* data, size_t len) { return file.write(data, len) == len; };
  // Reopening for write truncates: the server restarted the body from byte 0.
  sink.rewind = [&file, &partPath] {
    file.close();
    return Storage.openFileForWrite("HTTP", partPath.c_str(), file);
  };
  sink.progress = progress;

  size_t downloaded = 0;
  const DownloadError result =
      runGetSecure(url, username, password, headers, sink, cancelFlag, &downloaded, downgradeRedirectsToHttp);
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove. A failed rewind leaves no open handle.
  if (file.isOpen()) file.close();

  if (result != OK) {
    Storage.remove(partPath.c_str());
    return result;
  }
  if (downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    Storage.remove(partPath.c_str());
    return HTTP_ERROR;
  }
  if (!Storage.replaceFile(partPath.c_str(), destPath.c_str())) {
    LOG_ERR("HTTP", "Failed to move download into place: %s", destPath.c_str());
    Storage.remove(partPath.c_str());
    return FILE_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", downloaded);
  return OK;
}
