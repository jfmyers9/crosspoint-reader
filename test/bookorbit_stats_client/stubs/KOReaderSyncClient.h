#pragma once
#include <string>
extern std::string responseBody, requestBody, requestPath;
class KOReaderSyncClient {
 public:
  enum Error { OK };
  static Error postExtension(const char* s, const std::string& p, std::string& r) {
    requestPath = s;
    requestBody = p;
    r = responseBody;
    return OK;
  }
};
