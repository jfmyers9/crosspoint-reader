#pragma once
#include <string>
struct CredentialStub {
  std::string url = "http://books.test/api/v1/koreader";
  std::string user;
  bool credentials = true;
  bool hasCredentials() const { return credentials; }
  const std::string& getBaseUrl() const { return url; }
  const std::string& getUsername() const { return user; }
};
extern CredentialStub credentialStub;
#define KOREADER_STORE credentialStub
