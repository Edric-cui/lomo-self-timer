#include <M5Unified.h>
#include <Preferences.h>
#include "driver/rmt_common.h"
#include "driver/rmt_rx.h"
#include "../common/ir_backend.h"
#include "../common/ir_frame.h"

namespace {

constexpr size_t kMaxCaptureSymbols = 128;
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint32_t kSignalMinNs = 1000;
constexpr uint32_t kSignalMaxNs = 20000000;
constexpr size_t kMinReplayableSymbols = 4;
constexpr uint32_t kBackendSwitchNoticeMs = 500;

rmt_channel_handle_t g_rxChannel = nullptr;
rmt_symbol_word_t g_rxSymbols[kMaxCaptureSymbols];
volatile bool g_rxDone = false;
volatile size_t g_rxSymbolCount = 0;
uint32_t g_captureCount = 0;
Preferences g_preferences;
IrBackend g_backend = IrBackend::BuiltIn;

enum class CaptureValidation {
  kOk,
  kEmpty,
  kTooShort,
  kTooLong,
};

bool IRAM_ATTR onReceiveDone(
    rmt_channel_handle_t channel,
    const rmt_rx_done_event_data_t* eventData,
    void* userData
) {
  (void)channel;
  (void)userData;
  g_rxSymbolCount = eventData->num_symbols;
  g_rxDone = true;
  return true;
}

void beginReceive() {
  rmt_receive_config_t receiveConfig = {};
  receiveConfig.signal_range_min_ns = kSignalMinNs;
  receiveConfig.signal_range_max_ns = kSignalMaxNs;
  ESP_ERROR_CHECK(rmt_receive(
      g_rxChannel,
      g_rxSymbols,
      sizeof(g_rxSymbols),
      &receiveConfig
  ));
}

String backendLine() {
  return String("IR: ") + getIrBackendPins(g_backend).label;
}

void setupReceiver() {
  const IrBackendPins pins = getIrBackendPins(g_backend);
  rmt_rx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = pins.rx;
  channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  channelConfig.resolution_hz = kRmtResolutionHz;
  channelConfig.mem_block_symbols = kMaxCaptureSymbols;

  ESP_ERROR_CHECK(rmt_new_rx_channel(&channelConfig, &g_rxChannel));

  rmt_rx_event_callbacks_t callbacks = {};
  callbacks.on_recv_done = onReceiveDone;
  ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(g_rxChannel, &callbacks, nullptr));
  ESP_ERROR_CHECK(rmt_enable(g_rxChannel));
}

void teardownReceiver() {
  if (g_rxChannel == nullptr) {
    return;
  }

  rmt_disable(g_rxChannel);
  rmt_del_channel(g_rxChannel);
  g_rxChannel = nullptr;
}

void recreateReceiver() {
  g_rxDone = false;
  g_rxSymbolCount = 0;
  teardownReceiver();
  setupReceiver();
  beginReceive();
}

void printStatus(
    const String& line1,
    const String& line2 = String(),
    const String& line3 = String()
) {
  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.println("Lomo IR Capture");
  M5.Display.println();
  M5.Display.println(line1);
  if (!line2.isEmpty()) {
    M5.Display.println(line2);
  }
  if (!line3.isEmpty()) {
    M5.Display.println(line3);
  }
}

void showWaitingStatus() {
  printStatus("Waiting for IR", backendLine(), "BtnA switch IR");
}

void switchBackend(IrBackend backend) {
  if (backend == g_backend) {
    return;
  }

  g_backend = backend;
  saveIrBackendPreference(g_preferences, g_backend);
  recreateReceiver();
  printStatus("Backend updated", backendLine(), "Waiting for IR");
  delay(kBackendSwitchNoticeMs);
  showWaitingStatus();
}

CaptureValidation validateCapture(size_t symbolCount) {
  if (symbolCount == 0) {
    return CaptureValidation::kEmpty;
  }

  if (symbolCount < kMinReplayableSymbols) {
    return CaptureValidation::kTooShort;
  }

  if (symbolCount > kMaxReplaySymbols) {
    return CaptureValidation::kTooLong;
  }

  return CaptureValidation::kOk;
}

void printCaptureError(size_t symbolCount, CaptureValidation validation) {
  const uint32_t captureIndex = ++g_captureCount;

  Serial.println();
  Serial.printf("=== Capture %lu ===\n", static_cast<unsigned long>(captureIndex));
  Serial.printf("Symbol count: %u\n", static_cast<unsigned>(symbolCount));
  if (validation == CaptureValidation::kEmpty) {
    Serial.println("Capture rejected: frame is empty.");
    return;
  }

  if (validation == CaptureValidation::kTooShort) {
    Serial.printf(
        "Capture rejected: %u symbols is too short and looks like noise.\n",
        static_cast<unsigned>(symbolCount)
    );
    return;
  }

  Serial.printf(
      "Capture rejected: %u symbols exceeds replay limit of %u.\n",
      static_cast<unsigned>(symbolCount),
      static_cast<unsigned>(kMaxReplaySymbols)
  );
}

void printSymbolsAsArray(size_t symbolCount) {
  const uint32_t captureIndex = ++g_captureCount;

  Serial.println();
  Serial.printf("=== Capture %lu ===\n", static_cast<unsigned long>(captureIndex));
  Serial.printf("Symbol count: %u\n", static_cast<unsigned>(symbolCount));
  Serial.println("// Compare with firmware/common/replay_profile.h");
  Serial.printf("static const IrSymbol kLearnedCapture%lu[] = {\n", static_cast<unsigned long>(captureIndex));

  for (size_t i = 0; i < symbolCount; ++i) {
    const uint32_t d0 = g_rxSymbols[i].duration0;
    const uint32_t d1 = g_rxSymbols[i].duration1;
    const uint32_t l0 = g_rxSymbols[i].level0;
    const uint32_t l1 = g_rxSymbols[i].level1;
    const bool isLastSymbol = (i + 1 == symbolCount);

    Serial.printf(
        "  {%u, %u, %u, %u}%s\n",
        static_cast<unsigned>(d0),
        static_cast<unsigned>(d1),
        static_cast<unsigned>(l0),
        static_cast<unsigned>(l1),
        isLastSymbol ? "" : ","
    );
  }

  Serial.println("};");
  Serial.printf(
      "constexpr size_t kLearnedCapture%luCount = %u;\n",
      static_cast<unsigned long>(captureIndex),
      static_cast<unsigned>(symbolCount)
  );
}

void renderCaptureSummary(size_t symbolCount, CaptureValidation validation) {
  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.println("Lomo IR Capture");
  M5.Display.println();
  M5.Display.printf("Captured: %lu\n", static_cast<unsigned long>(g_captureCount));
  M5.Display.printf("Symbols:  %u\n", static_cast<unsigned>(symbolCount));
  M5.Display.println(backendLine());
  M5.Display.println();

  switch (validation) {
    case CaptureValidation::kOk:
      M5.Display.println("See Serial Monitor");
      break;
    case CaptureValidation::kEmpty:
      M5.Display.println("Rejected: empty");
      break;
    case CaptureValidation::kTooShort:
      M5.Display.println("Rejected: noise");
      break;
    case CaptureValidation::kTooLong:
      M5.Display.println("Rejected: too long");
      break;
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_spk = false;
  M5.begin(config);

  Serial.begin(115200);
  g_preferences.begin(kIrPrefsNamespace, false);
  g_backend = loadIrBackendPreference(g_preferences);
  M5.Power.setExtOutput(true);

  M5.Display.setRotation(3);
  M5.Display.setTextSize(2);
  showWaitingStatus();

  setupReceiver();
  beginReceive();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasClicked()) {
    switchBackend(nextIrBackend(g_backend));
  }

  if (!g_rxDone) {
    delay(10);
    return;
  }

  g_rxDone = false;
  const CaptureValidation validation = validateCapture(g_rxSymbolCount);
  if (validation == CaptureValidation::kOk) {
    printSymbolsAsArray(g_rxSymbolCount);
  } else {
    printCaptureError(g_rxSymbolCount, validation);
  }
  renderCaptureSummary(g_rxSymbolCount, validation);
  delay(250);
  showWaitingStatus();
  beginReceive();
}
