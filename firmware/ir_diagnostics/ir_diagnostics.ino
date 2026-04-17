#include <M5Unified.h>
#include <Preferences.h>
#include "driver/rmt_common.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "../common/ir_backend.h"
#include "../common/ir_frame.h"

namespace {

constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint32_t kCarrierDutyPercent = 33;
constexpr uint32_t kCarrierDutyCyclePercentDivisor = 100;
constexpr uint32_t kCarrierFrequency33kHz = 33000;
constexpr uint32_t kCarrierFrequency38kHz = 38000;
constexpr uint32_t kSignalMinNs = 1000;
constexpr uint32_t kSignalMaxNs = 20000000;
constexpr size_t kMaxCaptureSymbols = 128;
constexpr size_t kMinValidLoopbackSymbols = 4;
constexpr uint8_t kLoopbackAttempts = 3;
constexpr uint8_t kLoopbackPassThreshold = 2;
constexpr uint32_t kLoopbackArmDelayMs = 20;
constexpr uint32_t kLoopbackGapMs = 150;
constexpr uint32_t kLoopbackTimeoutMs = 1200;
constexpr uint8_t kBeaconRepeats = 4;
constexpr uint32_t kBeaconRepeatGapMs = 200;
constexpr char kDiagPrefsNamespace[] = "lomo-diag";
constexpr char kTxPrefsKey[] = "tx";
constexpr char kRxPrefsKey[] = "rx";

enum class TestMode : uint8_t {
  Beacon33k = 0,
  LoopSynthetic33k = 1,
  LoopLegacyRaw = 2,
  Beacon38k = 3,
  SweepU002 = 4,
};

enum class FocusField : uint8_t {
  TxBackend = 0,
  RxBackend = 1,
  TestMode = 2,
};

enum class SendEncoding {
  Direct,
  InvertDemodulated,
};

enum class FrameQuality {
  NoFrame,
  Noise,
  Valid,
};

struct SendFrameSpec {
  const IrSymbol* symbols;
  size_t count;
  uint32_t carrierHz;
  SendEncoding encoding;
  const char* hypothesisLabel;
};

struct LoopbackSummary {
  uint8_t validCount;
  size_t lastSymbolCount;
  FrameQuality lastQuality;
};

rmt_channel_handle_t g_txChannel = nullptr;
rmt_encoder_handle_t g_copyEncoder = nullptr;
rmt_channel_handle_t g_rxChannel = nullptr;
rmt_symbol_word_t g_rxSymbols[kMaxCaptureSymbols];
volatile bool g_rxDone = false;
volatile size_t g_rxSymbolCount = 0;
uint32_t g_currentCarrierHz = 0;

Preferences g_preferences;
IrBackend g_txBackend = IrBackend::BuiltIn;
IrBackend g_rxBackend = IrBackend::U002;
FocusField g_focus = FocusField::TxBackend;
TestMode g_testMode = TestMode::Beacon33k;
String g_resultLine1 = "Ready";
String g_resultLine2 = "BtnB run test";
uint32_t g_serialDumpIndex = 0;

static const IrSymbol kSyntheticShotSymbols[] = {
    {480, 7330, 1, 0},
    {480, 0, 1, 0},
};
constexpr size_t kSyntheticShotCount =
    sizeof(kSyntheticShotSymbols) / sizeof(kSyntheticShotSymbols[0]);

static const IrSymbol kLegacyCapture110[] = {
    {1268, 733, 0, 1},
    {953, 733, 0, 1},
    {197, 1206, 0, 1},
    {1267, 807, 0, 1},
    {821, 815, 0, 1},
    {194, 1183, 0, 1},
    {508, 1255, 0, 1},
    {432, 1204, 0, 1},
    {563, 1250, 0, 1},
    {378, 1235, 0, 1},
    {506, 1360, 0, 1},
    {1059, 7220, 0, 1},
    {1268, 758, 0, 1},
    {929, 706, 0, 1},
    {274, 1154, 0, 1},
    {1269, 832, 0, 1},
    {825, 708, 0, 1},
    {331, 1122, 0, 1},
    {535, 1152, 0, 1},
    {510, 1204, 0, 1},
    {537, 1149, 0, 1},
    {512, 1226, 0, 1},
    {482, 0, 0, 1},
};
constexpr size_t kLegacyCapture110Count =
    sizeof(kLegacyCapture110) / sizeof(kLegacyCapture110[0]);

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

const char* backendLabel(IrBackend backend) {
  return getIrBackendPins(backend).label;
}

const char* modeLabel(TestMode mode) {
  switch (mode) {
    case TestMode::Beacon33k:
      return "Beacon 33k";
    case TestMode::LoopSynthetic33k:
      return "Loop Syn33k";
    case TestMode::LoopLegacyRaw:
      return "Loop Legacy";
    case TestMode::Beacon38k:
      return "Beacon 38k";
    case TestMode::SweepU002:
      return "Sweep U002";
  }

  return "?";
}

const char* focusLabel(FocusField focus) {
  switch (focus) {
    case FocusField::TxBackend:
      return "TX";
    case FocusField::RxBackend:
      return "RX";
    case FocusField::TestMode:
      return "Mode";
  }

  return "?";
}

const char* qualityLabel(FrameQuality quality) {
  switch (quality) {
    case FrameQuality::NoFrame:
      return "No frame";
    case FrameQuality::Noise:
      return "Noise";
    case FrameQuality::Valid:
      return "Valid";
  }

  return "?";
}

IrBackend loadDiagBackendPreference(const char* key, IrBackend defaultBackend) {
  return normalizeIrBackend(g_preferences.getUChar(
      key,
      static_cast<uint8_t>(defaultBackend)
  ));
}

void saveDiagBackendPreference(const char* key, IrBackend backend) {
  g_preferences.putUChar(key, static_cast<uint8_t>(backend));
}

void setResult(const String& line1, const String& line2);

void drawScreen() {
  const char* hypothesis =
      (g_testMode == TestMode::LoopLegacyRaw) ? "Legacy" : "Syn33k";
  const IrBackendPins txPins = getIrBackendPins(g_txBackend);
  const IrBackendPins rxPins = getIrBackendPins(g_rxBackend);

  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.println("IR Diag");
  M5.Display.printf("%cTX:%s %d\n",
                    g_focus == FocusField::TxBackend ? '>' : ' ',
                    backendLabel(g_txBackend),
                    static_cast<int>(txPins.tx));
  M5.Display.printf("%cRX:%s %d\n",
                    g_focus == FocusField::RxBackend ? '>' : ' ',
                    backendLabel(g_rxBackend),
                    static_cast<int>(rxPins.rx));
  M5.Display.printf("%cMd:%s\n",
                    g_focus == FocusField::TestMode ? '>' : ' ',
                    modeLabel(g_testMode));
  M5.Display.printf("Hyp: %s\n", hypothesis);
  M5.Display.println(g_resultLine1);
  M5.Display.println(g_resultLine2);
}

void setResult(const String& line1, const String& line2) {
  g_resultLine1 = line1;
  g_resultLine2 = line2;
  drawScreen();
}

void setupTransmitter() {
  const IrBackendPins pins = getIrBackendPins(g_txBackend);
  rmt_tx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = pins.tx;
  channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  channelConfig.resolution_hz = kRmtResolutionHz;
  channelConfig.mem_block_symbols = 64;
  channelConfig.trans_queue_depth = 4;
  channelConfig.flags.invert_out = false;
  channelConfig.flags.with_dma = false;

  ESP_ERROR_CHECK(rmt_new_tx_channel(&channelConfig, &g_txChannel));

  rmt_copy_encoder_config_t copyConfig = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&copyConfig, &g_copyEncoder));
  ESP_ERROR_CHECK(rmt_enable(g_txChannel));
}

void teardownTransmitter() {
  if (g_txChannel != nullptr) {
    rmt_disable(g_txChannel);
    rmt_del_channel(g_txChannel);
    g_txChannel = nullptr;
  }

  if (g_copyEncoder != nullptr) {
    rmt_del_encoder(g_copyEncoder);
    g_copyEncoder = nullptr;
  }
}

void recreateTransmitter() {
  teardownTransmitter();
  setupTransmitter();
  g_currentCarrierHz = 0;
}

void applyCarrier(uint32_t carrierHz) {
  if (carrierHz == g_currentCarrierHz) {
    return;
  }

  rmt_carrier_config_t carrierConfig = {};
  carrierConfig.frequency_hz = carrierHz;
  carrierConfig.duty_cycle =
      static_cast<float>(kCarrierDutyPercent) /
      static_cast<float>(kCarrierDutyCyclePercentDivisor);
  carrierConfig.flags.polarity_active_low = false;
  ESP_ERROR_CHECK(rmt_apply_carrier(g_txChannel, &carrierConfig));
  g_currentCarrierHz = carrierHz;
}

void setupReceiver() {
  const IrBackendPins pins = getIrBackendPins(g_rxBackend);
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
}

bool armReceiver() {
  // If the previous loopback attempt timed out, the RX engine may still be
  // busy. Recreate the channel before every new arm so repeated tests don't
  // trip an invalid-state abort inside rmt_receive().
  recreateReceiver();
  g_rxDone = false;
  g_rxSymbolCount = 0;

  rmt_receive_config_t receiveConfig = {};
  receiveConfig.signal_range_min_ns = kSignalMinNs;
  receiveConfig.signal_range_max_ns = kSignalMaxNs;

  const esp_err_t result = rmt_receive(
      g_rxChannel,
      g_rxSymbols,
      sizeof(g_rxSymbols),
      &receiveConfig
  );

  if (result != ESP_OK) {
    Serial.printf("armReceiver rmt_receive error: %d\n", static_cast<int>(result));
    setResult("RX arm failed", String("esp_err ") + static_cast<int>(result));
    return false;
  }

  return true;
}

void switchTxBackend(IrBackend backend) {
  if (backend == g_txBackend) {
    return;
  }

  g_txBackend = backend;
  saveDiagBackendPreference(kTxPrefsKey, g_txBackend);
  recreateTransmitter();
  setResult(String("TX -> ") + backendLabel(g_txBackend), "Ready");
}

void switchRxBackend(IrBackend backend) {
  if (backend == g_rxBackend) {
    return;
  }

  g_rxBackend = backend;
  saveDiagBackendPreference(kRxPrefsKey, g_rxBackend);
  recreateReceiver();
  setResult(String("RX -> ") + backendLabel(g_rxBackend), "Ready");
}

void advanceFocus() {
  switch (g_focus) {
    case FocusField::TxBackend:
      g_focus = FocusField::RxBackend;
      break;
    case FocusField::RxBackend:
      g_focus = FocusField::TestMode;
      break;
    case FocusField::TestMode:
      g_focus = FocusField::TxBackend;
      break;
  }

  setResult(String("Focus: ") + focusLabel(g_focus), "BtnB run test");
}

void cycleFocusedSetting() {
  switch (g_focus) {
    case FocusField::TxBackend:
      switchTxBackend(nextIrBackend(g_txBackend));
      return;
    case FocusField::RxBackend:
      switchRxBackend(nextIrBackend(g_rxBackend));
      return;
    case FocusField::TestMode:
      g_testMode = static_cast<TestMode>((static_cast<uint8_t>(g_testMode) + 1) % 5);
      setResult(String("Mode: ") + modeLabel(g_testMode), "BtnB run test");
      return;
  }
}

size_t buildBeaconSymbols(IrSymbol* symbols, size_t capacity) {
  constexpr uint16_t kOnChunkUs = 25000;
  constexpr uint16_t kOffChunkUs = 30000;
  constexpr uint8_t kOnChunksPerBurst = 4;
  constexpr uint8_t kOffChunksPerGap = 5;
  constexpr uint8_t kBurstCount = 3;

  size_t index = 0;

  for (uint8_t burst = 0; burst < kBurstCount; ++burst) {
    for (uint8_t onChunk = 0; onChunk < kOnChunksPerBurst; ++onChunk) {
      if (index >= capacity) {
        return index;
      }

      symbols[index++] = {kOnChunkUs, kOnChunkUs, 1, 1};
    }

    for (uint8_t offChunk = 0; offChunk < kOffChunksPerGap; ++offChunk) {
      if (index >= capacity) {
        return index;
      }

      symbols[index++] = {kOffChunkUs, kOffChunkUs, 0, 0};
    }
  }

  return index;
}

SendFrameSpec getFrameSpec(TestMode mode) {
  switch (mode) {
    case TestMode::Beacon33k: {
      static IrSymbol beaconSymbols[kMaxReplaySymbols];
      static size_t beaconCount = 0;
      if (beaconCount == 0) {
        beaconCount = buildBeaconSymbols(beaconSymbols, kMaxReplaySymbols);
      }
      return {beaconSymbols, beaconCount, kCarrierFrequency33kHz, SendEncoding::Direct, "Synthetic 33k"};
    }
    case TestMode::LoopSynthetic33k:
      return {kSyntheticShotSymbols, kSyntheticShotCount, kCarrierFrequency33kHz,
              SendEncoding::Direct, "Synthetic 33k"};
    case TestMode::LoopLegacyRaw:
      return {kLegacyCapture110, kLegacyCapture110Count, kCarrierFrequency38kHz,
              SendEncoding::InvertDemodulated, "Legacy Raw"};
    case TestMode::Beacon38k:
    case TestMode::SweepU002: {
      static IrSymbol beaconSymbols38[kMaxReplaySymbols];
      static size_t beaconCount38 = 0;
      if (beaconCount38 == 0) {
        beaconCount38 = buildBeaconSymbols(beaconSymbols38, kMaxReplaySymbols);
      }
      return {beaconSymbols38, beaconCount38, kCarrierFrequency38kHz, SendEncoding::Direct, "Legacy Raw"};
    }
  }

  return {nullptr, 0, kCarrierFrequency38kHz, SendEncoding::Direct, "?"};
}

esp_err_t sendFrame(const SendFrameSpec& spec) {
  if (spec.symbols == nullptr || spec.count == 0 || spec.count > kMaxReplaySymbols) {
    return ESP_ERR_INVALID_ARG;
  }

  applyCarrier(spec.carrierHz);

  rmt_symbol_word_t encoded[kMaxReplaySymbols];
  for (size_t i = 0; i < spec.count; ++i) {
    if (spec.encoding == SendEncoding::InvertDemodulated) {
      encoded[i].level0 = spec.symbols[i].level0 ? 0 : 1;
      encoded[i].level1 = spec.symbols[i].level1 ? 0 : 1;
    } else {
      encoded[i].level0 = spec.symbols[i].level0;
      encoded[i].level1 = spec.symbols[i].level1;
    }

    encoded[i].duration0 = spec.symbols[i].duration0;
    encoded[i].duration1 = spec.symbols[i].duration1;
  }

  rmt_transmit_config_t transmitConfig = {};
  transmitConfig.loop_count = 0;
  transmitConfig.flags.eot_level = 0;

  esp_err_t result = rmt_transmit(
      g_txChannel,
      g_copyEncoder,
      encoded,
      spec.count * sizeof(rmt_symbol_word_t),
      &transmitConfig
  );

  if (result != ESP_OK) {
    return result;
  }

  return rmt_tx_wait_all_done(g_txChannel, 2000);
}

FrameQuality classifyLoopback(size_t symbolCount) {
  if (symbolCount == 0) {
    return FrameQuality::NoFrame;
  }

  if (symbolCount < kMinValidLoopbackSymbols) {
    return FrameQuality::Noise;
  }

  return FrameQuality::Valid;
}

void printCapturedSymbols(const char* label, size_t symbolCount) {
  ++g_serialDumpIndex;
  Serial.println();
  Serial.printf("=== %s %lu ===\n",
                label,
                static_cast<unsigned long>(g_serialDumpIndex));
  Serial.printf("Symbol count: %u\n", static_cast<unsigned>(symbolCount));
  Serial.printf("static const IrSymbol kDiagCapture%lu[] = {\n",
                static_cast<unsigned long>(g_serialDumpIndex));

  for (size_t i = 0; i < symbolCount; ++i) {
    Serial.printf(
        "  {%u, %u, %u, %u}%s\n",
        static_cast<unsigned>(g_rxSymbols[i].duration0),
        static_cast<unsigned>(g_rxSymbols[i].duration1),
        static_cast<unsigned>(g_rxSymbols[i].level0),
        static_cast<unsigned>(g_rxSymbols[i].level1),
        (i + 1 == symbolCount) ? "" : ","
    );
  }

  Serial.println("};");
  Serial.printf("constexpr size_t kDiagCapture%luCount = %u;\n",
                static_cast<unsigned long>(g_serialDumpIndex),
                static_cast<unsigned>(symbolCount));
}

LoopbackSummary runLoopbackSeries(const SendFrameSpec& spec, const char* serialLabel) {
  LoopbackSummary summary = {};
  summary.lastQuality = FrameQuality::NoFrame;

  for (uint8_t attempt = 0; attempt < kLoopbackAttempts; ++attempt) {
    setResult(String(modeLabel(g_testMode)) + " " +
                  String(attempt + 1) + "/" + String(kLoopbackAttempts),
              String(spec.hypothesisLabel) + " TX " + backendLabel(g_txBackend) +
                  " -> RX " + backendLabel(g_rxBackend));

    if (!armReceiver()) {
      delay(kLoopbackGapMs);
      continue;
    }
    delay(kLoopbackArmDelayMs);

    const esp_err_t sendResult = sendFrame(spec);
    if (sendResult != ESP_OK) {
      summary.lastQuality = FrameQuality::NoFrame;
      summary.lastSymbolCount = 0;
      Serial.printf("%s send error: %d\n", serialLabel, static_cast<int>(sendResult));
      delay(kLoopbackGapMs);
      continue;
    }

    const uint32_t startedAt = millis();
    while (!g_rxDone && (millis() - startedAt) < kLoopbackTimeoutMs) {
      delay(10);
    }

    if (!g_rxDone) {
      summary.lastQuality = FrameQuality::NoFrame;
      summary.lastSymbolCount = 0;
      Serial.printf("%s timed out waiting for RX.\n", serialLabel);
      delay(kLoopbackGapMs);
      continue;
    }

    summary.lastSymbolCount = g_rxSymbolCount;
    summary.lastQuality = classifyLoopback(g_rxSymbolCount);
    if (summary.lastQuality == FrameQuality::Valid) {
      ++summary.validCount;
    }

    printCapturedSymbols(serialLabel, g_rxSymbolCount);
    delay(kLoopbackGapMs);
  }

  return summary;
}

void runBeaconTest(uint32_t expectedCarrierHz) {
  const SendFrameSpec spec = getFrameSpec(g_testMode);
  const int txPin = static_cast<int>(getIrBackendPins(g_txBackend).tx);
  setResult(String("Aim cam 5cm"),
            String("TX") + txPin + " " + String(expectedCarrierHz / 1000) + "k");

  for (uint8_t repeatIndex = 0; repeatIndex < kBeaconRepeats; ++repeatIndex) {
    const esp_err_t sendResult = sendFrame(spec);
    if (sendResult != ESP_OK) {
      setResult("Beacon failed", String("esp_err ") + static_cast<int>(sendResult));
      return;
    }

    Serial.printf("Beacon sent on %s pin %d at %u Hz (%u/%u).\n",
                  backendLabel(g_txBackend),
                  txPin,
                  static_cast<unsigned>(expectedCarrierHz),
                  static_cast<unsigned>(repeatIndex + 1),
                  static_cast<unsigned>(kBeaconRepeats));

    if (repeatIndex + 1 < kBeaconRepeats) {
      setResult(String("Beacon ") + String(repeatIndex + 1) + "/" + String(kBeaconRepeats),
                String("TX") + txPin + " " + String(expectedCarrierHz / 1000) + "k");
      delay(kBeaconRepeatGapMs);
    }
  }

  setResult(String("Beacon done"),
            String("TX") + txPin + " " + String(expectedCarrierHz / 1000) + "k");
}

void runLoopbackTest() {
  const SendFrameSpec spec = getFrameSpec(g_testMode);
  const char* label =
      (g_testMode == TestMode::LoopSynthetic33k) ? "LoopSynthetic33k" : "LoopLegacyRaw";
  const LoopbackSummary summary = runLoopbackSeries(spec, label);

  if (summary.validCount >= kLoopbackPassThreshold) {
    setResult(String(spec.hypothesisLabel) + " pass " + String(summary.validCount) + "/3",
              String("Last: ") + String(summary.lastSymbolCount) + " sym");
    return;
  }

  if (summary.lastQuality == FrameQuality::NoFrame) {
    setResult(String(spec.hypothesisLabel) + " no RX",
              "Check TX pin/power");
  } else if (summary.lastQuality == FrameQuality::Noise) {
    setResult(String(spec.hypothesisLabel) + " only noise",
              "Has activity, not valid");
  } else {
    setResult(String(spec.hypothesisLabel) + " weak",
              String("Valid: ") + String(summary.validCount) + "/3");
  }
}

void runSweepTest() {
  constexpr uint8_t kSweepRepeats = 12;
  constexpr uint32_t kSweepRepeatGapMs = 500;
  constexpr uint32_t kSweepPinPauseMs = 3000;
  constexpr uint32_t kSweepPreWarningMs = 2000;

  const gpio_num_t candidatePins[] = {
      static_cast<gpio_num_t>(10),
      static_cast<gpio_num_t>(9),
      static_cast<gpio_num_t>(20),
      static_cast<gpio_num_t>(19)
  };
  constexpr size_t pinCount = 4;
  const SendFrameSpec spec = getFrameSpec(g_testMode);

  for (size_t pinIdx = 0; pinIdx < pinCount; ++pinIdx) {
    const gpio_num_t pin = candidatePins[pinIdx];
    int pinInt = static_cast<int>(pin);

    // Pre-warning: show which pin is next so user can position camera
    setResult(String("NEXT: Pin ") + pinInt,
              String("Starts in 2s (") + String(pinIdx + 1) + "/" + String(pinCount) + ")");
    Serial.printf("\n>>> Next sweep pin: GPIO %d (%u/%u) — get camera ready!\n",
                  pinInt,
                  static_cast<unsigned>(pinIdx + 1),
                  static_cast<unsigned>(pinCount));
    delay(kSweepPreWarningMs);

    teardownTransmitter();

    rmt_tx_channel_config_t channelConfig = {};
    channelConfig.gpio_num = pin;
    channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
    channelConfig.resolution_hz = kRmtResolutionHz;
    channelConfig.mem_block_symbols = 64;
    channelConfig.trans_queue_depth = 4;
    channelConfig.flags.invert_out = false;
    channelConfig.flags.with_dma = false;

    if (rmt_new_tx_channel(&channelConfig, &g_txChannel) != ESP_OK) {
      setResult(String("Sweep SKIP ") + pinInt, "Channel create failed");
      Serial.printf(">>> Sweep: failed to create TX channel on GPIO %d, skipping.\n", pinInt);
      delay(kSweepPinPauseMs);
      continue;
    }

    rmt_copy_encoder_config_t copyConfig = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copyConfig, &g_copyEncoder));
    ESP_ERROR_CHECK(rmt_enable(g_txChannel));

    g_currentCarrierHz = 0;

    setResult(String("FIRING Pin ") + pinInt,
              String("Burst ") + "1/" + String(kSweepRepeats));

    for (uint8_t repeatIndex = 0; repeatIndex < kSweepRepeats; ++repeatIndex) {
      const esp_err_t sendResult = sendFrame(spec);
      if (sendResult != ESP_OK) {
        setResult(String("Pin ") + pinInt + " ERR",
                  String("esp_err ") + static_cast<int>(sendResult));
        Serial.printf(">>> Sweep pin %d: send error %d at burst %u\n",
                      pinInt, static_cast<int>(sendResult),
                      static_cast<unsigned>(repeatIndex + 1));
        break;
      }

      setResult(String("FIRING Pin ") + pinInt,
                String("Burst ") + String(repeatIndex + 1) + "/" + String(kSweepRepeats));

      Serial.printf(">>> Sweep pin %d: burst %u/%u sent at %u Hz\n",
                    pinInt,
                    static_cast<unsigned>(repeatIndex + 1),
                    static_cast<unsigned>(kSweepRepeats),
                    static_cast<unsigned>(spec.carrierHz));

      delay(kSweepRepeatGapMs);
    }

    setResult(String("Pin ") + pinInt + " done",
              String("Pause 3s..."));
    delay(kSweepPinPauseMs);
  }

  setResult("Sweep done", "Which pin lit up?");
  recreateTransmitter();
}

void runCurrentTest() {
  switch (g_testMode) {
    case TestMode::Beacon33k:
      runBeaconTest(kCarrierFrequency33kHz);
      break;
    case TestMode::LoopSynthetic33k:
    case TestMode::LoopLegacyRaw:
      runLoopbackTest();
      break;
    case TestMode::Beacon38k:
      runBeaconTest(kCarrierFrequency38kHz);
      break;
    case TestMode::SweepU002:
      runSweepTest();
      break;
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_spk = false;
  M5.begin(config);

  Serial.begin(115200);
  g_preferences.begin(kDiagPrefsNamespace, false);
  g_txBackend = loadDiagBackendPreference(kTxPrefsKey, IrBackend::BuiltIn);
  g_rxBackend = loadDiagBackendPreference(kRxPrefsKey, IrBackend::U002);

  M5.Power.setExtOutput(true);
  M5.Display.setRotation(3);
  M5.Display.setTextSize(2);

  setupTransmitter();
  setupReceiver();
  drawScreen();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasHold()) {
    advanceFocus();
  } else if (M5.BtnA.wasClicked()) {
    cycleFocusedSetting();
  }

  if (M5.BtnB.wasPressed()) {
    runCurrentTest();
  }

  delay(10);
}
