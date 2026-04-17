#pragma once

#include <Preferences.h>
#include "driver/gpio.h"

enum class IrBackend : uint8_t {
  BuiltIn = 0,
  U002 = 1,
};

struct IrBackendPins {
  gpio_num_t tx;
  gpio_num_t rx;
  const char* label;
};

constexpr const char* kIrPrefsNamespace = "lomo-ir";
constexpr const char* kIrPrefsBackendKey = "backend";

inline constexpr IrBackendPins getIrBackendPins(IrBackend backend) {
  switch (backend) {
    case IrBackend::U002:
      return {GPIO_NUM_9, GPIO_NUM_10, "U002"};
    case IrBackend::BuiltIn:
    default:
      return {GPIO_NUM_46, GPIO_NUM_42, "Built-in"};
  }
}

inline constexpr IrBackend nextIrBackend(IrBackend backend) {
  return (backend == IrBackend::BuiltIn) ? IrBackend::U002 : IrBackend::BuiltIn;
}

inline constexpr IrBackend normalizeIrBackend(uint8_t raw) {
  return (raw == static_cast<uint8_t>(IrBackend::U002))
      ? IrBackend::U002
      : IrBackend::BuiltIn;
}

inline IrBackend loadIrBackendPreference(Preferences& preferences) {
  return normalizeIrBackend(
      preferences.getUChar(
          kIrPrefsBackendKey,
          static_cast<uint8_t>(IrBackend::BuiltIn)
      )
  );
}

inline void saveIrBackendPreference(Preferences& preferences, IrBackend backend) {
  preferences.putUChar(kIrPrefsBackendKey, static_cast<uint8_t>(backend));
}
