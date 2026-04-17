#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t kMaxReplaySymbols = 96;

struct IrSymbol {
  uint16_t duration0;
  uint16_t duration1;
  uint8_t level0;
  uint8_t level1;
};
