#pragma once

#include <string>

// Exercise ArduinoJson's real Arduino String writer/reader adapters on the host.
class String {
 public:
  String() = default;
  String(const std::string& value) : value(value) {}
  String(const char* value) : value(value ? value : "") {}
  String& operator=(const char* text) {
    value = text ? text : "";
    return *this;
  }
  bool concat(const char* text) {
    value += text;
    return true;
  }
  bool isEmpty() const { return value.empty(); }
  const char* c_str() const { return value.c_str(); }
  size_t length() const { return value.length(); }

 private:
  std::string value;
};
