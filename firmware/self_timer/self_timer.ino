#include <M5Unified.h>
#include "driver/rmt_common.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "../common/ir_backend.h"
#include "../common/ir_frame.h"
#include "../common/replay_profile.h"

namespace {

constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint8_t kSpeakerVolume = 192;
constexpr bool kEnableDebugBeeps = true;

constexpr uint8_t kDelayOptionsSeconds[] = {3, 5, 10, 15, 20};
size_t g_delayIndex = 0;
bool g_countdownActive = false;
uint32_t g_countdownEndMs = 0;
int g_lastRenderedSeconds = -1;
String g_status = "Ready";

rmt_channel_handle_t g_txChannel = nullptr;
rmt_encoder_handle_t g_copyEncoder = nullptr;
rmt_channel_handle_t g_txChannel2 = nullptr;
rmt_encoder_handle_t g_copyEncoder2 = nullptr;

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

  // Skip the final countdown beep so the last audible cue does not overlap
  // with the IR trigger moment.
  if (remainingSeconds == 1) {
    return;
  }

  if (remainingSeconds <= 3) {
    playBeep(1760.0f, 120);
    return;
  }

  // Split the countdown into long / medium / final phases so the user can
  // hear roughly how much setup time remains without looking at the screen.
  if (remainingSeconds <= 10) {
    playBeep(1046.5f, 70);
    return;
  }

  playBeep(784.0f, 55);
}

void drawScreen() {
  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.println("Lomo Timer");
  M5.Display.printf("Dly:%us IR:Dual\n",
                    kDelayOptionsSeconds[g_delayIndex]);
  M5.Display.println("Code:Ready");

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
    M5.Display.println("B start");
  }
}

void setStatus(const String& status) {
  g_status = status;
  drawScreen();
}

void setupOneChannel(gpio_num_t pin, rmt_channel_handle_t* channel,
                     rmt_encoder_handle_t* encoder) {
  rmt_tx_channel_config_t channelConfig = {};
  channelConfig.gpio_num = pin;
  channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
  channelConfig.resolution_hz = kRmtResolutionHz;
  channelConfig.mem_block_symbols = 64;
  channelConfig.trans_queue_depth = 4;
  channelConfig.flags.invert_out = false;
  channelConfig.flags.with_dma = false;

  ESP_ERROR_CHECK(rmt_new_tx_channel(&channelConfig, channel));

  rmt_carrier_config_t carrierConfig = {};
  carrierConfig.frequency_hz = replay_profile::kCarrierFrequencyHz;
  carrierConfig.duty_cycle = replay_profile::kCarrierDutyCycle;
  carrierConfig.flags.polarity_active_low = false;
  ESP_ERROR_CHECK(rmt_apply_carrier(*channel, &carrierConfig));

  rmt_copy_encoder_config_t copyConfig = {};
  ESP_ERROR_CHECK(rmt_new_copy_encoder(&copyConfig, encoder));
  ESP_ERROR_CHECK(rmt_enable(*channel));
}

void teardownOneChannel(rmt_channel_handle_t* channel,
                        rmt_encoder_handle_t* encoder) {
  if (*channel != nullptr) {
    rmt_disable(*channel);
    rmt_del_channel(*channel);
    *channel = nullptr;
  }

  if (*encoder != nullptr) {
    rmt_del_encoder(*encoder);
    *encoder = nullptr;
  }
}

void setupTransmitter() {
  const IrBackendPins builtInPins = getIrBackendPins(IrBackend::BuiltIn);
  const IrBackendPins u002Pins = getIrBackendPins(IrBackend::U002);
  setupOneChannel(builtInPins.tx, &g_txChannel, &g_copyEncoder);
  setupOneChannel(u002Pins.tx, &g_txChannel2, &g_copyEncoder2);
  Serial.printf("Dual TX: BuiltIn (GPIO %d) + U002 (GPIO %d)\n",
                static_cast<int>(builtInPins.tx),
                static_cast<int>(u002Pins.tx));
}

void teardownTransmitter() {
  teardownOneChannel(&g_txChannel, &g_copyEncoder);
  teardownOneChannel(&g_txChannel2, &g_copyEncoder2);
}

void recreateTransmitter() {
  teardownTransmitter();
  setupTransmitter();
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

  const size_t byteCount = count * sizeof(rmt_symbol_word_t);

  // Fire primary TX channel
  esp_err_t result = rmt_transmit(
      g_txChannel,
      g_copyEncoder,
      encoded,
      byteCount,
      &transmitConfig
  );

  if (result != ESP_OK) {
    return SendResult::kTransmitError;
  }

  // Fire secondary TX channel simultaneously
  if (g_txChannel2 != nullptr) {
    rmt_transmit(
        g_txChannel2,
        g_copyEncoder2,
        encoded,
        byteCount,
        &transmitConfig
    );
    // Best-effort: don't fail the whole send if secondary has issues
  }

  result = rmt_tx_wait_all_done(g_txChannel, 1000);
  if (g_txChannel2 != nullptr) {
    rmt_tx_wait_all_done(g_txChannel2, 1000);
  }

  return (result == ESP_OK) ? SendResult::kOk : SendResult::kTransmitError;
}

SendResult sendSymbols(const IrSymbol* symbols, size_t count) {
  for (uint8_t repeatIndex = 0;
       repeatIndex < replay_profile::kSendRepeats;
       ++repeatIndex) {
    const SendResult result = sendSymbolsOnce(symbols, count);
    if (result != SendResult::kOk) {
      return result;
    }

    if (repeatIndex + 1 < replay_profile::kSendRepeats) {
      delay(replay_profile::kRepeatGapMs);
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

  const SendResult result = sendSymbols(
      replay_profile::kInstantSymbols,
      replay_profile::kInstantSymbolCount
  );
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

  if (!g_countdownActive && M5.BtnA.wasClicked()) {
    g_delayIndex = (g_delayIndex + 1) %
        (sizeof(kDelayOptionsSeconds) / sizeof(kDelayOptionsSeconds[0]));
    setStatus("Delay updated");
  }

  if (M5.BtnB.wasPressed()) {
    if (g_countdownActive) {
      cancelCountdown();
    } else {
      startCountdown();
    }
  }

  maybeRenderCountdownTick();
  maybeFireShot();
  delay(10);
}
