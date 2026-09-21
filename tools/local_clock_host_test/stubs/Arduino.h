#pragma once
// Minimal Arduino stubs for host-side LocalClock tests.
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>

struct SerialStub {
  template <typename... A>
  void printf(const char*, A...) {}
};
inline SerialStub Serial;

using std::isfinite;

// Enough of Arduino String for settings.h inline helpers.
class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  bool isEmpty() const { return s_.empty(); }
  const char* c_str() const { return s_.c_str(); }
  String& operator=(const char* s) {
    s_ = s ? s : "";
    return *this;
  }

 private:
  std::string s_;
};

struct EspStub {
  uint64_t getEfuseMac() const { return 0xC49E7E0733F4ULL; }
};
inline EspStub ESP;
