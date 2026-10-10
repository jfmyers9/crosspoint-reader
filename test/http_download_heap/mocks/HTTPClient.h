#pragma once
#include <HalStorage.h>
#include <TailscaleNetworkClient.h>

#include <map>
#include <string>
#include <vector>

class String : public std::string {
 public:
  using std::string::string;
  String(const std::string& value) : std::string(value) {}
  bool isEmpty() const { return empty(); }
};
constexpr int HTTP_CODE_OK = 200;
namespace tailnet_test {
struct Response {
  int status = 200;
  std::string body = "feed";
  std::string location;
};
struct Request {
  std::string url;
  std::string basicAuth;
  std::map<std::string, std::string> headers;
};
inline std::vector<Response> responses;
inline std::vector<Request> requests;
inline bool beginSucceeds = true;
}  // namespace tailnet_test

class HTTPClient {
 public:
  void setConnectTimeout(int) {}
  void setTimeout(int) {}
  void setUserAgent(const char*) {}
  void setAuthorization(const char* value) { request.basicAuth = value; }
  void setAuthorization(const char* user, const char* password) {
    request.basicAuth = std::string(user) + ":" + password;
  }
  bool begin(TailscaleNetworkClient&, const char* url) {
    request.url = url;
    return tailnet_test::beginSucceeds;
  }
  void addHeader(const char* name, const char* value) { request.headers[name] = value; }
  int GET() {
    const auto index = tailnet_test::requests.size();
    tailnet_test::requests.push_back(request);
    response = index < tailnet_test::responses.size() ? tailnet_test::responses[index] : tailnet_test::Response{};
    return response.status;
  }
  String getLocation() const { return response.location; }
  int getSize() const { return static_cast<int>(response.body.size()); }
  int writeToStream(Stream* output) {
    const auto written = output->write(reinterpret_cast<const uint8_t*>(response.body.data()), response.body.size());
    return written == response.body.size() ? static_cast<int>(written) : -1;
  }
  void end() { request = {}; }

 private:
  tailnet_test::Request request;
  tailnet_test::Response response;
};
