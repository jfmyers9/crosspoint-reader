#pragma once
#include <string>
extern std::string responseBody, requestBody, requestPath;
extern bool transportFails;
extern int postCount;
class KOReaderSyncClient {
 public:
  enum Error { OK, FAILED };
  static Error postExtension(const char* suffix, const std::string& payload, std::string& response) {
    ++postCount;
    requestPath = suffix;
    requestBody = payload;
    response = responseBody;
    return transportFails ? FAILED : OK;
  }
};
