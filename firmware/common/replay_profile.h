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
constexpr uint8_t kBulbSendRepeats = 1;

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

// Captured on 2026-04-20 from the original TIME button with a stable
// single-frame outcome across 6 accepted samples. This is a candidate for
// empty-camera validation; public docs should still treat bulb support as
// unvalidated until the camera body confirms open/close behavior.
static const IrSymbol kBulbOpenSymbols[] = {
    {1170, 518, 0, 1},
    {1199, 488, 0, 1},
    {498, 1193, 0, 1},
    {1220, 490, 0, 1},
    {1199, 489, 0, 1},
    {471, 1221, 0, 1},
    {519, 1190, 0, 1},
    {497, 1191, 0, 1},
    {1196, 516, 0, 1},
    {498, 1190, 0, 1},
    {497, 1195, 0, 1},
    {518, 7993, 0, 1},
    {1196, 492, 0, 1},
    {1198, 489, 0, 1},
    {496, 1198, 0, 1},
    {1218, 489, 0, 1},
    {1199, 491, 0, 1},
    {520, 1190, 0, 1},
    {499, 1189, 0, 1},
    {499, 1194, 0, 1},
    {1219, 490, 0, 1},
    {498, 1190, 0, 1},
    {495, 1217, 0, 1},
    {499, 0, 0, 1},
};

constexpr size_t kBulbOpenSymbolCount =
    sizeof(kBulbOpenSymbols) / sizeof(kBulbOpenSymbols[0]);

constexpr bool kHasBulbCandidateProfile = true;
constexpr bool kHasValidatedBulbProfile = false;

inline ReplayFrame bulbOpenFrame() {
  return {kBulbOpenSymbols, kBulbOpenSymbolCount};
}

inline ReplayFrame bulbCloseFrame() {
  return {kBulbOpenSymbols, kBulbOpenSymbolCount};
}

}  // namespace replay_profile
