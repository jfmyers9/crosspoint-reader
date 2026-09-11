#pragma once
constexpr int WL_CONNECTED = 1;
struct WifiStub {
  int status() { return WL_CONNECTED; }
};
inline WifiStub WiFi;
