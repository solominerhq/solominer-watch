#pragma once
// Host-only adapter for the small Arduino String surface used by settings.
// No hardware, networking or persistence is mocked by the policy tests.
#include <string>
#include <ctype.h>
class String {
  std::string value_;
public:
  String(const char* value = "") : value_(value ? value : "") {}
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  void trim() {
    size_t a=0, b=value_.size();
    while(a<b && isspace(static_cast<unsigned char>(value_[a]))) ++a;
    while(b>a && isspace(static_cast<unsigned char>(value_[b-1]))) --b;
    value_=value_.substr(a,b-a);
  }
};
