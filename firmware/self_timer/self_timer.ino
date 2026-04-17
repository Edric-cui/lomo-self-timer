#include <M5Unified.h>
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "../common/ir_frame.h"

namespace {

constexpr gpio_num_t kIrSendPin = GPIO_NUM_46;
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint32_t kCarrierFrequencyHz = 38000;
constexpr float kCarrierDutyCycle = 0.33f;
constexpr uint8_t kSpeakerVolume = 192;

constexpr uint8_t kDelayOptionsSeconds[] = {3, 5, 10};
size_t g_delayIndex = 0;
bool g_countdownActive = false;
uint32_t g_countdownEndMs = 0;
int g_lastRenderedSeconds = -1;
String g_status = "Ready";

rmt_channel_handle_t g_txChannel = nullptr;
rmt_encoder_handle_t g_copyEncoder = nullptr;

// Replace this block with the IrSymbol capture printed by firmware/ir_capture/ir_capture.ino.
constexpr bool kHasInstantCode = true;
static const IrSymbol kInstantSymbols[] = {
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
constexpr size_t kInstantSymbolCount =
    sizeof(kInstantSymbols) / sizeof(kInstantSymbols[0]);

enum class SendResult {
  kOk,
  kInvalid,
  kTooLong,
  kTransmitError,
};

void playBeep(float frequencyHz, uint32_t durationMs) {
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
  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.println("Lomo Self Timer");
  M5.Display.println();
  M5.Display.printf("Delay: %us\n", kDelayOptionsSeconds[g_delayIndex]);
  M5.Display.printf("Code:  %s\n", kHasInstantCode ? "Ready" : "Missing");
  M5.Display.println();

  if (g_countdownActive) {
    const uint32_t now = millis();
    const uint32_t remainingMs =
        (g_countdownEndMs > now) ? (g_countdownEndMs - now) : 0;
    const uint32_t remainingSeconds = (remainingMs + 999) / 1000;
    M5.Display.printf("Go in: %lus\n", static_cast<unsigned long>(remainingSeconds));
  } else {
    M5.Display.println("Go in: -");
  }

  M5.Display.println();
  M5.Display.printf("Status: %s\n", g_status.c_str());
  M5.Display.println();
  if (g_countdownActive) {
    M5.Display.println("BtnA locked");
    M5.Display.println("BtnB cancel");
  } else {
    M5.Display.println("BtnA delay");
    M5.Display.println("BtnB start");
  }
}

void setStatus(const String& status) {
  g_status = status;
  drawScreen();
}

void setupTransmitter() {
  rmt_tx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = kIrSendPin;
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

SendResult sendSymbols(const IrSymbol* symbols, size_t count) {
  if (count == 0) {
    return SendResult::kInvalid;
  }

  if (count > kMaxReplaySymbols) {
    return SendResult::kTooLong;
  }

  rmt_symbol_word_t encoded[kMaxReplaySymbols];

  for (size_t i = 0; i < count; ++i) {
    encoded[i].level0 = symbols[i].level0 ? 1 : 0;
    encoded[i].duration0 = symbols[i].duration0;
    encoded[i].level1 = symbols[i].level1 ? 1 : 0;
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
  M5.begin(config);

  M5.Power.setExtOutput(true, m5::ext_none);
  M5.Display.setRotation(3);
  M5.Display.setTextSize(2);
  M5.Speaker.setVolume(kSpeakerVolume);

  setupTransmitter();
  drawScreen();
}

void loop() {
  M5.update();

  if (!g_countdownActive && M5.BtnA.wasPressed()) {
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
