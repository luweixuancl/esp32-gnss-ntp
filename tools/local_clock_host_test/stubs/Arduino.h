#pragma once
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

class String : public std::string {
 public:
  String() = default;
  String(const char* s) : std::string(s ? s : "") {}
  bool isEmpty() const { return empty(); }
};

struct EspStub {
  uint64_t getEfuseMac() const { return 0x9EC4ull; }
};
inline EspStub ESP;

using std::isfinite;
