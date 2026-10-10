#pragma once
#include <string>
struct TailscaleManagerStub {
  bool ready = true;
  bool isMagicDnsUrl(const std::string& url) const { return url.find(".ts.net") != std::string::npos; }
  bool prepareUrl(const std::string&) const { return ready; }
};
inline TailscaleManagerStub TAILSCALE;
