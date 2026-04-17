#include <M5Unified.h>
#include <Preferences.h>
#include "driver/rmt_common.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "../common/ir_backend.h"
#include "../common/ir_frame.h"

namespace {

constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint32_t kCarrierFrequencyHz = 38000;
constexpr float kCarrierDutyCycle = 0.33f;
constexpr uint8_t kSpeakerVolume = 192;
constexpr uint8_t kSendRepeats = 1;
constexpr uint32_t kRepeatGapMs = 30;
constexpr bool kEnableDebugBeeps = false;

constexpr uint8_t kDelayOptionsSeconds[] = {3, 5, 10};
size_t g_delayIndex = 0;
bool g_countdownActive = false;
uint32_t g_countdownEndMs = 0;
int g_lastRenderedSeconds = -1;
String g_status = "Ready";

rmt_channel_handle_t g_txChannel = nullptr;
rmt_encoder_handle_t g_copyEncoder = nullptr;
Preferences g_preferences;
IrBackend g_backend = IrBackend::BuiltIn;

// Current best candidate: an earlier 24-symbol capture that was notably more
// stable than the later fragmented traces.
constexpr bool kHasInstantCode = true;
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

enum class SendResult {
  kOk,
  kInvalid,
  kTooLong,
  kTransmitError,
};

void playBeep(float frequencyHz, uint32_t durationMs) {
  if (!kEnableDebugBeeps) {
    return;
  }

  M5.Speaker.tone(frequencyHz, durationMs);
}

void playCountdownBeep(int remainingSeconds) {
  if (remainingSeconds <= 0) {
    return;
  }

  if (remainingSeconds <= 3) {
    playBeep(1760.0f, 120);
    return;
  }

  playBeep(1046.5f, 70);
}

void drawScreen() {
  const IrBackendPins pins = getIrBackendPins(g_backend);
  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.println("Lomo Timer");
  M5.Display.printf("Dly:%us IR:%s\n",
                    kDelayOptionsSeconds[g_delayIndex],
                    pins.label);
  M5.Display.printf("Code:%s\n", kHasInstantCode ? "Ready" : "Miss");

  if (g_countdownActive) {
    const uint32_t now = millis();
    const uint32_t remainingMs =
        (g_countdownEndMs > now) ? (g_countdownEndMs - now) : 0;
    const uint32_t remainingSeconds = (remainingMs + 999) / 1000;
    M5.Display.printf("Go:%lus\n", static_cast<unsigned long>(remainingSeconds));
  } else {
    M5.Display.println("Go:-");
  }

  M5.Display.printf("St:%s\n", g_status.c_str());
  if (g_countdownActive) {
    M5.Display.println("A lock");
    M5.Display.println("B cancel");
  } else {
    M5.Display.println("A tap delay");
    M5.Display.println("A hold IR");
    M5.Display.println("B start");
  }
}

void setStatus(const String& status) {
  g_status = status;
  drawScreen();
}

void setupTransmitter() {
  const IrBackendPins pins = getIrBackendPins(g_backend);
  rmt_tx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = pins.tx;
  channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  channelConfig.resolution_hz = kRmtResolutionHz;
  channelConfig.mem_block_symbols = 64;
  channelConfig.trans_queue_depth = 4;
  channelConfig.flags.invert_out = false;
  channelConfig.flags.with_dma = false;

  ESP_ERROR_CHECK(rmt_new_tx_channel(&channelConfig, &g_txChannel));

  rmt_carrier_config_t carrierConfig = {};
  carrierConfig.frequency_hz = kCarrierFrequencyHz;
  carrierConfig.duty_cycle = kCarrierDutyCycle;
  carrierConfig.flags.polarity_active_low = false;
  ESP_ERROR_CHECK(rmt_apply_carrier(g_txChannel, &carrierConfig));

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
}

void switchBackend(IrBackend backend) {
  if (backend == g_backend) {
    return;
  }

  g_backend = backend;
  saveIrBackendPreference(g_preferences, g_backend);
  recreateTransmitter();
  setStatus(String("IR: ") + getIrBackendPins(g_backend).label);
}

SendResult sendSymbolsOnce(const IrSymbol* symbols, size_t count) {
  if (count == 0) {
    return SendResult::kInvalid;
  }

  if (count > kMaxReplaySymbols) {
    return SendResult::kTooLong;
  }

  rmt_symbol_word_t encoded[kMaxReplaySymbols];

  for (size_t i = 0; i < count; ++i) {
    // Demodulating IR receivers typically output active-low pulses, so the
    // captured levels need to be inverted before driving the carrier LED.
    encoded[i].level0 = symbols[i].level0 ? 0 : 1;
    encoded[i].duration0 = symbols[i].duration0;
    encoded[i].level1 = symbols[i].level1 ? 0 : 1;
    encoded[i].duration1 = symbols[i].duration1;
  }

  rmt_transmit_config_t transmitConfig = {};
  transmitConfig.loop_count = 0;
  transmitConfig.flags.eot_level = 0;

  esp_err_t result = rmt_transmit(
      g_txChannel,
      g_copyEncoder,
      encoded,
      count * sizeof(rmt_symbol_word_t),
      &transmitConfig
  );

  if (result != ESP_OK) {
    return SendResult::kTransmitError;
  }

  result = rmt_tx_wait_all_done(g_txChannel, 1000);
  return (result == ESP_OK) ? SendResult::kOk : SendResult::kTransmitError;
}

SendResult sendSymbols(const IrSymbol* symbols, size_t count) {
  for (uint8_t repeatIndex = 0; repeatIndex < kSendRepeats; ++repeatIndex) {
    const SendResult result = sendSymbolsOnce(symbols, count);
    if (result != SendResult::kOk) {
      return result;
    }

    if (repeatIndex + 1 < kSendRepeats) {
      delay(kRepeatGapMs);
    }
  }

  return SendResult::kOk;
}

void startCountdown() {
  g_countdownActive = true;
  g_countdownEndMs = millis() + (kDelayOptionsSeconds[g_delayIndex] * 1000UL);
  g_lastRenderedSeconds = -1;
  playBeep(1318.5f, 90);
  setStatus("Counting down");
}

void cancelCountdown() {
  g_countdownActive = false;
  g_lastRenderedSeconds = -1;
  playBeep(440.0f, 140);
  setStatus("Cancelled");
}

void maybeRenderCountdownTick() {
  if (!g_countdownActive) {
    return;
  }

  const uint32_t now = millis();
  const uint32_t remainingMs =
      (g_countdownEndMs > now) ? (g_countdownEndMs - now) : 0;
  const int remainingSeconds = static_cast<int>((remainingMs + 999) / 1000);

  if (remainingSeconds != g_lastRenderedSeconds) {
    g_lastRenderedSeconds = remainingSeconds;
    playCountdownBeep(remainingSeconds);
    drawScreen();
  }
}

void maybeFireShot() {
  if (!g_countdownActive) {
    return;
  }

  if (millis() < g_countdownEndMs) {
    return;
  }

  g_countdownActive = false;
  g_lastRenderedSeconds = -1;
  setStatus("Sending...");

  const SendResult result = sendSymbols(kInstantSymbols, kInstantSymbolCount);
  const bool sent = result == SendResult::kOk;
  playBeep(sent ? 2093.0f : 220.0f, sent ? 140 : 220);

  switch (result) {
    case SendResult::kOk:
      setStatus("Done");
      break;
    case SendResult::kTooLong:
      setStatus("Code too long");
      break;
    case SendResult::kInvalid:
      setStatus("Code invalid");
      break;
    case SendResult::kTransmitError:
      setStatus("Send failed");
      break;
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_spk = kEnableDebugBeeps;
  M5.begin(config);

  g_preferences.begin(kIrPrefsNamespace, false);
  g_backend = loadIrBackendPreference(g_preferences);
  M5.Power.setExtOutput(true);
  M5.Display.setRotation(3);
  M5.Display.setTextSize(2);
  if (kEnableDebugBeeps) {
    M5.Speaker.setVolume(kSpeakerVolume);
  }

  setupTransmitter();
  drawScreen();
}

void loop() {
  M5.update();

  if (!g_countdownActive && M5.BtnA.wasHold()) {
    switchBackend(nextIrBackend(g_backend));
  } else if (!g_countdownActive && M5.BtnA.wasClicked()) {
    g_delayIndex = (g_delayIndex + 1) %
        (sizeof(kDelayOptionsSeconds) / sizeof(kDelayOptionsSeconds[0]));
    setStatus("Delay updated");
  }

  if (M5.BtnB.wasPressed()) {
    if (g_countdownActive) {
      cancelCountdown();
    } else if (!kHasInstantCode) {
      setStatus("No code");
    } else {
      startCountdown();
    }
  }

  maybeRenderCountdownTick();
  maybeFireShot();
  delay(10);
}
