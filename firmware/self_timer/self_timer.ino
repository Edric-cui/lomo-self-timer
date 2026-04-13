#include <M5Unified.h>
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

namespace {

constexpr gpio_num_t kIrSendPin = GPIO_NUM_46;
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint32_t kCarrierFrequencyHz = 38000;
constexpr float kCarrierDutyCycle = 0.33f;
constexpr size_t kMaxEncodedSymbols = 96;
constexpr uint8_t kSpeakerVolume = 96;

constexpr uint8_t kDelayOptionsSeconds[] = {3, 5, 10};
size_t g_delayIndex = 0;
bool g_countdownActive = false;
uint32_t g_countdownEndMs = 0;
int g_lastRenderedSeconds = -1;
String g_status = "Ready";

rmt_channel_handle_t g_txChannel = nullptr;
rmt_encoder_handle_t g_copyEncoder = nullptr;

// Replace this block with the raw durations printed by firmware/ir_capture/ir_capture.ino.
// Keep the sequence as alternating mark, space, mark, space in microseconds.
constexpr bool kHasInstantCode = false;
static const uint16_t kInstantDurationsUs[] = {
    9008, 4488, 591, 568, 538, 567, 565, 567
};
constexpr size_t kInstantDurationsCount =
    sizeof(kInstantDurationsUs) / sizeof(kInstantDurationsUs[0]);

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
  M5.Display.println("BtnA delay");
  M5.Display.println("BtnB start/cancel");
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

bool sendRawDurations(const uint16_t* durations, size_t count) {
  if (count < 2 || count > (kMaxEncodedSymbols * 2)) {
    return false;
  }

  rmt_symbol_word_t symbols[kMaxEncodedSymbols];
  size_t symbolCount = 0;

  for (size_t i = 0; i < count && symbolCount < kMaxEncodedSymbols; i += 2) {
    symbols[symbolCount].level0 = 1;
    symbols[symbolCount].duration0 = durations[i];
    symbols[symbolCount].level1 = 0;
    symbols[symbolCount].duration1 = (i + 1 < count) ? durations[i + 1] : 0;
    ++symbolCount;
  }

  rmt_transmit_config_t transmitConfig = {};
  transmitConfig.loop_count = 0;
  transmitConfig.flags.eot_level = 0;

  esp_err_t result = rmt_transmit(
      g_txChannel,
      g_copyEncoder,
      symbols,
      symbolCount * sizeof(rmt_symbol_word_t),
      &transmitConfig
  );

  if (result != ESP_OK) {
    return false;
  }

  result = rmt_tx_wait_all_done(g_txChannel, 1000);
  return result == ESP_OK;
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

  const bool sent = sendRawDurations(kInstantDurationsUs, kInstantDurationsCount);
  playBeep(sent ? 2093.0f : 220.0f, sent ? 140 : 220);
  setStatus(sent ? "Done" : "Send failed");
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

  if (M5.BtnA.wasPressed()) {
    g_delayIndex = (g_delayIndex + 1) %
        (sizeof(kDelayOptionsSeconds) / sizeof(kDelayOptionsSeconds[0]));
    if (!g_countdownActive) {
      setStatus("Delay updated");
    } else {
      drawScreen();
    }
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
