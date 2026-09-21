#pragma once
#include <cstdint>

// Minimal IPv4 holder so include/settings.h compiles on host.
class IPAddress {
 public:
  IPAddress() : a_(0), b_(0), c_(0), d_(0) {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : a_(a), b_(b), c_(c), d_(d) {}

 private:
  uint8_t a_, b_, c_, d_;
};
