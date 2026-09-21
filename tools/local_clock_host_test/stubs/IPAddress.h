#pragma once
#include <cstdint>

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    oct_[0] = a;
    oct_[1] = b;
    oct_[2] = c;
    oct_[3] = d;
  }

 private:
  uint8_t oct_[4] = {};
};
