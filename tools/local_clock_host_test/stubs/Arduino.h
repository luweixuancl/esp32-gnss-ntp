#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
struct SerialStub {
  template <typename... A> void printf(const char*, A...) {}
};
inline SerialStub Serial;
using std::isfinite;
