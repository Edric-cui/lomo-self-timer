#include <M5Unified.h>
#include "driver/rmt_common.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "../common/ir_backend.h"
#include "../common/ir_frame.h"
#include "../common/replay_profile.h"

namespace {

constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint8_t kSpeakerVolume = 255;
constexpr bool kEnableDebugBeeps = true;
constexpr uint32_t kBulbCountdownMs = 3000;
constexpr uint8_t kBulbOpenRetries = 1;
constexpr uint8_t kBulbCloseRetries = 2;
constexpr uint32_t kElapsedRenderStepMs = 250;
constexpr uint32_t kIdleAutoPowerOffMs = 5UL * 60UL * 1000UL;
constexpr int16_t kUsbPresentThresholdMv = 4000;

constexpr uint8_t kShotCountdownOptionsSeconds[] = {3, 5, 10, 15, 20};
constexpr uint32_t kBulbExposureOptionsMs[] = {
    500, 750, 1000, 1250, 1500, 2000, 3000, 4000, 8000, 15000, 28000
};

enum class TriggerMode : uint8_t {
  Shot = 0,
  Bulb = 1,
};

enum class RuntimeState : uint8_t {
  Idle = 0,
  Countdown = 1,
  SendingShot = 2,
  BulbOpening = 3,
  BulbOpen = 4,
  BulbClosing = 5,
  Error = 6,
};

enum class ErrorReason : uint8_t {
  None = 0,
  OpenFailed = 1,
  CloseFailed = 2,
};

enum class SendResult {
  kOk,
  kInvalid,
  kTooLong,
  kTransmitError,
};

size_t g_shotDelayIndex = 0;
size_t g_bulbExposureIndex = 0;
TriggerMode g_triggerMode = TriggerMode::Shot;
RuntimeState g_runtimeState = RuntimeState::Idle;
ErrorReason g_errorReason = ErrorReason::None;
uint32_t g_countdownEndMs = 0;
uint32_t g_bulbOpenedAtMs = 0;
uint32_t g_bulbCloseDeadlineMs = 0;
uint32_t g_lastUserActivityMs = 0;
int g_lastRenderedCountdownSeconds = -1;
int g_lastRenderedElapsedBucket = -1;

rmt_channel_handle_t g_txChannel = nullptr;
rmt_encoder_handle_t g_copyEncoder = nullptr;
rmt_channel_handle_t g_txChannel2 = nullptr;
rmt_encoder_handle_t g_copyEncoder2 = nullptr;

const char* triggerModeLabel(TriggerMode mode) {
  switch (mode) {
    case TriggerMode::Shot:
      return "SHOT";
    case TriggerMode::Bulb:
      return "BULB";
  }

  return "?";
}

const char* errorReasonLabel(ErrorReason reason) {
  switch (reason) {
    case ErrorReason::None:
      return "-";
    case ErrorReason::OpenFailed:
      return "open fail";
    case ErrorReason::CloseFailed:
      return "close fail";
  }

  return "?";
}

uint8_t currentShotCountdownSeconds() {
  return kShotCountdownOptionsSeconds[g_shotDelayIndex];
}

uint32_t currentBulbExposureMs() {
  return kBulbExposureOptionsMs[g_bulbExposureIndex];
}

uint32_t currentCountdownDurationMs() {
  if (g_triggerMode == TriggerMode::Bulb) {
    return kBulbCountdownMs;
  }

  return static_cast<uint32_t>(currentShotCountdownSeconds()) * 1000UL;
}

void formatDurationMs(uint32_t durationMs, char* buffer, size_t bufferSize) {
  const unsigned long wholeSeconds = durationMs / 1000UL;
  const uint32_t fractionalMs = durationMs % 1000UL;

  if (fractionalMs == 0) {
    snprintf(buffer, bufferSize, "%lus", wholeSeconds);
    return;
  }

  if (fractionalMs == 250) {
    snprintf(buffer, bufferSize, "%lu.25s", wholeSeconds);
    return;
  }

  if (fractionalMs == 500) {
    snprintf(buffer, bufferSize, "%lu.5s", wholeSeconds);
    return;
  }

  if (fractionalMs == 750) {
    snprintf(buffer, bufferSize, "%lu.75s", wholeSeconds);
    return;
  }

  snprintf(buffer, bufferSize, "%lu.%03lus", wholeSeconds, fractionalMs);
}

void printCurrentPresetLine() {
  if (g_triggerMode == TriggerMode::Shot) {
    M5.Display.printf("Dly:%us IR:Dual\n", currentShotCountdownSeconds());
    return;
  }

  char exposureLabel[16];
  formatDurationMs(currentBulbExposureMs(), exposureLabel, sizeof(exposureLabel));
  M5.Display.printf("Exp:%s IR:Dual\n", exposureLabel);
}

void formatBatteryLabel(char* buffer, size_t bufferSize) {
  int32_t batteryLevel = M5.Power.getBatteryLevel();
  if (batteryLevel > 100) {
    batteryLevel = 100;
  } else if (batteryLevel < 0) {
    batteryLevel = -1;
  }

  const bool isCharging =
      M5.Power.isCharging() == m5::Power_Class::is_charging;
  if (batteryLevel < 0) {
    snprintf(buffer, bufferSize, "%s", isCharging ? "USB" : "Bat?");
    return;
  }

  snprintf(buffer, bufferSize, "%ld%%%s",
           static_cast<long>(batteryLevel),
           isCharging ? "+" : "");
}

void drawTitleLine() {
  char batteryLabel[8];
  formatBatteryLabel(batteryLabel, sizeof(batteryLabel));

  M5.Display.setCursor(0, 0);
  M5.Display.print("Lomo Timer");

  const int16_t batteryX =
      M5.Display.width() - M5.Display.textWidth(batteryLabel);
  if (batteryX > M5.Display.getCursorX() + 4) {
    M5.Display.setCursor(batteryX, 0);
    M5.Display.print(batteryLabel);
  }

  M5.Display.setCursor(0, M5.Display.fontHeight());
}

bool bulbProfileReady() {
  const replay_profile::ReplayFrame openFrame = replay_profile::bulbOpenFrame();
  const replay_profile::ReplayFrame closeFrame = replay_profile::bulbCloseFrame();
  return replay_profile::kHasBulbCandidateProfile &&
      openFrame.symbols != nullptr &&
      openFrame.count > 0 &&
      closeFrame.symbols != nullptr &&
      closeFrame.count > 0;
}

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

  if (remainingSeconds == 1) {
    return;
  }

  if (remainingSeconds <= 3) {
    playBeep(1760.0f, 120);
    return;
  }

  if (remainingSeconds <= 10) {
    playBeep(1046.5f, 110);
    return;
  }

  playBeep(784.0f, 95);
}

void playStartTone() {
  playBeep(1318.5f, 90);
}

void playCancelTone() {
  playBeep(440.0f, 140);
}

void playShotSuccessTone() {
  playBeep(2093.0f, 140);
}

void playSendFailureTone() {
  playBeep(220.0f, 220);
}

void playBulbOpenTone() {
  playBeep(1568.0f, 120);
}

void playBulbCloseTone() {
  playBeep(1174.0f, 140);
}

void playLatchedErrorAlarm() {
  playBeep(330.0f, 180);
  delay(60);
  playBeep(262.0f, 220);
}

void markUserActivity() {
  g_lastUserActivityMs = millis();
}

bool usbPowerPresent() {
  if (M5.Power.getVBUSVoltage() >= kUsbPresentThresholdMv) {
    return true;
  }

  return M5.Power.isCharging() == m5::Power_Class::is_charging;
}

uint32_t countdownRemainingMs() {
  const uint32_t now = millis();
  return (g_countdownEndMs > now) ? (g_countdownEndMs - now) : 0;
}

int countdownRemainingSeconds() {
  return static_cast<int>((countdownRemainingMs() + 999) / 1000UL);
}

uint32_t bulbElapsedMs() {
  if (g_bulbOpenedAtMs == 0) {
    return 0;
  }

  return millis() - g_bulbOpenedAtMs;
}

uint32_t displayBulbElapsedMs() {
  return (bulbElapsedMs() / kElapsedRenderStepMs) * kElapsedRenderStepMs;
}

void drawScreen() {
  M5.Display.clear();
  drawTitleLine();
  M5.Display.printf("Mode:%s\n", triggerModeLabel(g_triggerMode));
  printCurrentPresetLine();

  switch (g_runtimeState) {
    case RuntimeState::Idle:
      if (g_triggerMode == TriggerMode::Shot && !bulbProfileReady()) {
        M5.Display.println("St:Bulb unavailable");
      } else {
        M5.Display.println("St:Ready");
      }
      M5.Display.println(g_triggerMode == TriggerMode::Shot ?
          "Front Btn tap delay" : "Front Btn tap exp");
      if (g_triggerMode == TriggerMode::Shot) {
        M5.Display.println(bulbProfileReady() ?
            "Front Btn hold BULB" : "Front Btn ignored");
      } else {
        M5.Display.println("Front Btn hold SHOT");
      }
      M5.Display.println("Side Btn start");
      break;
    case RuntimeState::Countdown:
      M5.Display.printf("Go:%ds\n", countdownRemainingSeconds());
      M5.Display.println("St:Counting");
      M5.Display.println("Front Btn ignored");
      M5.Display.println("Side Btn cancel");
      break;
    case RuntimeState::SendingShot:
      M5.Display.println("Go:0s");
      M5.Display.println("St:Shot send");
      M5.Display.println("Btns ignored");
      break;
    case RuntimeState::BulbOpening:
      M5.Display.println("Go:0s");
      M5.Display.println("St:Open send");
      M5.Display.println("Btns ignored");
      break;
    case RuntimeState::BulbOpen: {
      char elapsedLabel[16];
      formatDurationMs(displayBulbElapsedMs(), elapsedLabel, sizeof(elapsedLabel));
      M5.Display.printf("Open:%s\n", elapsedLabel);
      M5.Display.println("St:Presumed");
      M5.Display.println("Front Btn ignored");
      M5.Display.println("Side Btn close");
      break;
    }
    case RuntimeState::BulbClosing:
      M5.Display.println("St:Close send");
      M5.Display.println("Btns ignored");
      break;
    case RuntimeState::Error:
      M5.Display.printf("Err:%s\n", errorReasonLabel(g_errorReason));
      M5.Display.println("St:reset Btns");
      M5.Display.println("Boot -> SHOT");
      break;
  }
}

void setupOneChannel(gpio_num_t pin,
                     rmt_channel_handle_t* channel,
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

SendResult sendSymbolsOnce(const IrSymbol* symbols, size_t count) {
  if (count == 0 || symbols == nullptr) {
    return SendResult::kInvalid;
  }

  if (count > kMaxReplaySymbols) {
    return SendResult::kTooLong;
  }

  rmt_symbol_word_t encoded[kMaxReplaySymbols];
  for (size_t i = 0; i < count; ++i) {
    encoded[i].level0 = symbols[i].level0 ? 0 : 1;
    encoded[i].duration0 = symbols[i].duration0;
    encoded[i].level1 = symbols[i].level1 ? 0 : 1;
    encoded[i].duration1 = symbols[i].duration1;
  }

  rmt_transmit_config_t transmitConfig = {};
  transmitConfig.loop_count = 0;
  transmitConfig.flags.eot_level = 0;

  const size_t byteCount = count * sizeof(rmt_symbol_word_t);

  esp_err_t result = rmt_transmit(
      g_txChannel,
      g_copyEncoder,
      encoded,
      byteCount,
      &transmitConfig
  );
  if (result != ESP_OK) {
    Serial.printf("Primary TX start failed: %d\n", static_cast<int>(result));
    return SendResult::kTransmitError;
  }

  if (g_txChannel2 != nullptr) {
    const esp_err_t secondaryResult = rmt_transmit(
        g_txChannel2,
        g_copyEncoder2,
        encoded,
        byteCount,
        &transmitConfig
    );
    if (secondaryResult != ESP_OK) {
      Serial.printf("Secondary TX start failed: %d\n",
                    static_cast<int>(secondaryResult));
    }
  }

  result = rmt_tx_wait_all_done(g_txChannel, 1000);
  if (result != ESP_OK) {
    Serial.printf("Primary TX wait failed: %d\n", static_cast<int>(result));
    return SendResult::kTransmitError;
  }

  if (g_txChannel2 != nullptr) {
    const esp_err_t secondaryWait = rmt_tx_wait_all_done(g_txChannel2, 1000);
    if (secondaryWait != ESP_OK) {
      Serial.printf("Secondary TX wait failed: %d\n",
                    static_cast<int>(secondaryWait));
    }
  }

  return SendResult::kOk;
}

SendResult sendSymbols(const IrSymbol* symbols,
                       size_t count,
                       uint8_t repeatCount) {
  for (uint8_t repeatIndex = 0;
       repeatIndex < repeatCount;
       ++repeatIndex) {
    const SendResult result = sendSymbolsOnce(symbols, count);
    if (result != SendResult::kOk) {
      return result;
    }

    if (repeatIndex + 1 < repeatCount) {
      delay(replay_profile::kRepeatGapMs);
    }
  }

  return SendResult::kOk;
}

SendResult sendFrame(const replay_profile::ReplayFrame& frame,
                     uint8_t repeatCount) {
  return sendSymbols(frame.symbols, frame.count, repeatCount);
}

SendResult sendFrameWithRetries(const replay_profile::ReplayFrame& frame,
                                uint8_t repeatCount,
                                uint8_t additionalRetries) {
  SendResult result = SendResult::kInvalid;
  for (uint8_t attempt = 0; attempt <= additionalRetries; ++attempt) {
    result = sendFrame(frame, repeatCount);
    if (result == SendResult::kOk) {
      return result;
    }

    if (attempt < additionalRetries) {
      delay(80);
    }
  }

  return result;
}

void resetTimers() {
  g_countdownEndMs = 0;
  g_bulbOpenedAtMs = 0;
  g_bulbCloseDeadlineMs = 0;
  g_lastRenderedCountdownSeconds = -1;
  g_lastRenderedElapsedBucket = -1;
}

void enterIdle(TriggerMode mode) {
  g_triggerMode = mode;
  g_runtimeState = RuntimeState::Idle;
  g_errorReason = ErrorReason::None;
  resetTimers();
  markUserActivity();
  drawScreen();
}

void enterError(ErrorReason reason) {
  g_runtimeState = RuntimeState::Error;
  g_errorReason = reason;
  resetTimers();
  drawScreen();
  playLatchedErrorAlarm();
}

void cycleIdlePreset() {
  if (g_triggerMode == TriggerMode::Shot) {
    g_shotDelayIndex = (g_shotDelayIndex + 1) %
        (sizeof(kShotCountdownOptionsSeconds) /
         sizeof(kShotCountdownOptionsSeconds[0]));
  } else {
    g_bulbExposureIndex = (g_bulbExposureIndex + 1) %
        (sizeof(kBulbExposureOptionsMs) / sizeof(kBulbExposureOptionsMs[0]));
  }

  drawScreen();
}

void toggleTriggerMode() {
  if (g_triggerMode == TriggerMode::Shot) {
    if (!bulbProfileReady()) {
      playSendFailureTone();
      drawScreen();
      return;
    }
    g_triggerMode = TriggerMode::Bulb;
  } else {
    g_triggerMode = TriggerMode::Shot;
  }

  drawScreen();
}

void startCountdown() {
  g_runtimeState = RuntimeState::Countdown;
  g_errorReason = ErrorReason::None;
  g_countdownEndMs = millis() + currentCountdownDurationMs();
  g_lastRenderedCountdownSeconds = -1;
  g_lastRenderedElapsedBucket = -1;
  playStartTone();
  drawScreen();
}

void cancelCountdown() {
  playCancelTone();
  enterIdle(g_triggerMode);
}

void runShotSend() {
  g_runtimeState = RuntimeState::SendingShot;
  drawScreen();

  const SendResult result = sendFrame(replay_profile::kInstantFrame,
                                      replay_profile::kSendRepeats);
  if (result == SendResult::kOk) {
    playShotSuccessTone();
  } else {
    playSendFailureTone();
  }

  enterIdle(TriggerMode::Shot);
}

void runBulbOpen() {
  g_runtimeState = RuntimeState::BulbOpening;
  drawScreen();

  const SendResult result = sendFrameWithRetries(replay_profile::bulbOpenFrame(),
                                                 replay_profile::kBulbSendRepeats,
                                                 kBulbOpenRetries);
  if (result != SendResult::kOk) {
    enterError(ErrorReason::OpenFailed);
    return;
  }

  g_runtimeState = RuntimeState::BulbOpen;
  g_errorReason = ErrorReason::None;
  g_countdownEndMs = 0;
  g_bulbOpenedAtMs = millis();
  g_bulbCloseDeadlineMs = g_bulbOpenedAtMs + currentBulbExposureMs();
  g_lastRenderedElapsedBucket = -1;
  playBulbOpenTone();
  drawScreen();
}

void runBulbClose() {
  g_runtimeState = RuntimeState::BulbClosing;
  drawScreen();

  const SendResult result = sendFrameWithRetries(replay_profile::bulbCloseFrame(),
                                                 replay_profile::kBulbSendRepeats,
                                                 kBulbCloseRetries);
  if (result != SendResult::kOk) {
    enterError(ErrorReason::CloseFailed);
    return;
  }

  playBulbCloseTone();
  enterIdle(TriggerMode::Bulb);
}

void maybeRenderCountdownTick() {
  if (g_runtimeState != RuntimeState::Countdown) {
    return;
  }

  const int remainingSeconds = countdownRemainingSeconds();
  if (remainingSeconds != g_lastRenderedCountdownSeconds) {
    g_lastRenderedCountdownSeconds = remainingSeconds;
    playCountdownBeep(remainingSeconds);
    drawScreen();
  }
}

void maybeRenderBulbElapsedTick() {
  if (g_runtimeState != RuntimeState::BulbOpen) {
    return;
  }

  const int elapsedBucket = static_cast<int>(displayBulbElapsedMs() /
      kElapsedRenderStepMs);
  if (elapsedBucket != g_lastRenderedElapsedBucket) {
    g_lastRenderedElapsedBucket = elapsedBucket;
    drawScreen();
  }
}

void advanceRuntime() {
  if (g_runtimeState == RuntimeState::Countdown &&
      millis() >= g_countdownEndMs) {
    if (g_triggerMode == TriggerMode::Shot) {
      runShotSend();
    } else {
      runBulbOpen();
    }
    return;
  }

  if (g_runtimeState == RuntimeState::BulbOpen &&
      g_bulbCloseDeadlineMs != 0 &&
      static_cast<int32_t>(millis() - g_bulbCloseDeadlineMs) >= 0) {
    runBulbClose();
  }
}

void autoPowerOff() {
  M5.Display.clear();
  drawTitleLine();
  M5.Display.println("Auto power off");
  M5.Display.println("Idle 5 min");
  playBeep(880.0f, 120);
  delay(180);
  M5.Power.setExtOutput(false);
  M5.Power.powerOff();
  while (true) {
    delay(1000);
  }
}

void maybeAutoPowerOff() {
  if (g_runtimeState != RuntimeState::Idle || usbPowerPresent()) {
    return;
  }

  const uint32_t powerOffDeadlineMs =
      g_lastUserActivityMs + kIdleAutoPowerOffMs;
  if (static_cast<int32_t>(millis() - powerOffDeadlineMs) >= 0) {
    autoPowerOff();
  }
}

void handleButtons() {
  if (g_runtimeState == RuntimeState::Idle) {
    if (M5.BtnA.wasHold()) {
      markUserActivity();
      toggleTriggerMode();
      return;
    }

    if (M5.BtnA.wasClicked()) {
      markUserActivity();
      cycleIdlePreset();
    }

    if (M5.BtnB.wasPressed()) {
      markUserActivity();
      startCountdown();
    }
    return;
  }

  if (g_runtimeState == RuntimeState::Countdown) {
    if (M5.BtnB.wasPressed()) {
      markUserActivity();
      cancelCountdown();
    }
    return;
  }

  if (g_runtimeState == RuntimeState::BulbOpen) {
    if (M5.BtnB.wasPressed()) {
      markUserActivity();
      runBulbClose();
    }
    return;
  }

  if (g_runtimeState == RuntimeState::Error) {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
      markUserActivity();
      enterIdle(TriggerMode::Shot);
    }
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_spk = kEnableDebugBeeps;
  M5.begin(config);

  Serial.begin(115200);
  M5.Power.setExtOutput(true);
  M5.Display.setRotation(3);
  M5.Display.setTextSize(2);
  if (kEnableDebugBeeps) {
    M5.Speaker.setVolume(kSpeakerVolume);
  }

  setupTransmitter();
  enterIdle(TriggerMode::Shot);
}

void loop() {
  M5.update();
  handleButtons();
  maybeRenderCountdownTick();
  maybeRenderBulbElapsedTick();
  advanceRuntime();
  maybeAutoPowerOff();
  delay(10);
}
