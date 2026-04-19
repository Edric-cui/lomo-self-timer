#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ir_frame.h"

namespace replay_profile {

struct ReplayFrame {
  const IrSymbol* symbols;
  size_t count;
};

constexpr uint32_t kCarrierFrequencyHz = 38000;
constexpr float kCarrierDutyCycle = 0.50f;
constexpr uint8_t kSendRepeats = 5;
constexpr uint32_t kRepeatGapMs = 50;

// Validated on 2026-04-18 with a Lomo'Instant Wide Glass body.
static const IrSymbol kInstantSymbols[] = {
    {1158, 530, 0, 1},
    {1156, 583, 0, 1},
    {455, 1181, 0, 1},
    {1182, 531, 0, 1},
    {1156, 556, 0, 1},
    {457, 1231, 0, 1},
    {455, 1258, 0, 1},
    {453, 1233, 0, 1},
    {428, 1235, 0, 1},
    {481, 1231, 0, 1},
    {480, 1258, 0, 1},
    {1156, 7280, 0, 1},
    {1205, 508, 0, 1},
    {1183, 556, 0, 1},
    {428, 1233, 0, 1},
    {1105, 607, 0, 1},
    {1154, 533, 0, 1},
    {455, 1234, 0, 1},
    {453, 1285, 0, 1},
    {455, 1206, 0, 1},
    {455, 1233, 0, 1},
    {479, 1208, 0, 1},
    {482, 1230, 0, 1},
    {1158, 0, 0, 1},
};

constexpr size_t kInstantSymbolCount =
    sizeof(kInstantSymbols) / sizeof(kInstantSymbols[0]);

constexpr ReplayFrame kInstantFrame = {kInstantSymbols, kInstantSymbolCount};

// Bulb remains locked until the TIME button waveform is captured, classified,
// and validated on an empty camera body. Do not replace these placeholders with
// speculative arrays.
constexpr bool kHasValidatedBulbProfile = false;

inline ReplayFrame bulbOpenFrame() {
  return {nullptr, 0};
}

inline ReplayFrame bulbCloseFrame() {
  return {nullptr, 0};
}

}  // namespace replay_profile
