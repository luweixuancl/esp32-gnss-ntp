#pragma once
#include <cstdint>
enum class AnomalyPolicy : uint8_t {
  Refuse = 0,
  HoldoverShort = 1,
  HoldoverLong = 2,
};
inline float tempCoeffPpmPerC(int16_t centi) {
  return static_cast<float>(centi) / 100.0f;
}
