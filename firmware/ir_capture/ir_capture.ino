#include <M5Unified.h>
#include <Preferences.h>
#include "driver/rmt_common.h"
#include "driver/rmt_rx.h"
#include "../common/ir_backend.h"
#include "../common/ir_frame.h"
#include "../common/replay_profile.h"

namespace {

constexpr size_t kMaxCaptureSymbols = 128;
constexpr uint32_t kRmtResolutionHz = 1000000;
constexpr uint32_t kSignalMinNs = 1000;
constexpr uint32_t kSignalMaxNs = 20000000;
constexpr size_t kMinReplayableSymbols = 4;
constexpr uint32_t kBackendSwitchNoticeMs = 500;
constexpr size_t kMaxAcceptedCaptures = 10;
constexpr uint16_t kGapThresholdUs = 3000;
constexpr uint8_t kRelationTolerancePercent = 15;

enum class CaptureValidation {
  kOk,
  kEmpty,
  kTooShort,
  kTooLong,
};

enum class CaptureCluster {
  kNone,
  kA,
  kB,
};

enum class LearningOutcome {
  kPending,
  kMatchesInstant,
  kSingleDistinct,
  kOddEvenDual,
  kInconclusive,
};

struct StoredCapture {
  bool used;
  CaptureCluster cluster;
  uint32_t captureIndex;
  size_t symbolCount;
  IrSymbol symbols[kMaxReplaySymbols];
};

rmt_channel_handle_t g_rxChannel = nullptr;
rmt_symbol_word_t g_rxSymbols[kMaxCaptureSymbols];
volatile bool g_rxDone = false;
volatile size_t g_rxSymbolCount = 0;
uint32_t g_captureCount = 0;
Preferences g_preferences;
IrBackend g_backend = IrBackend::BuiltIn;
StoredCapture g_acceptedCaptures[kMaxAcceptedCaptures] = {};
size_t g_acceptedCaptureCount = 0;
LearningOutcome g_learningOutcome = LearningOutcome::kPending;

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

const char* clusterLabel(CaptureCluster cluster) {
  switch (cluster) {
    case CaptureCluster::kNone:
      return "-";
    case CaptureCluster::kA:
      return "A";
    case CaptureCluster::kB:
      return "B";
  }

  return "?";
}

const char* outcomeLabel(LearningOutcome outcome) {
  switch (outcome) {
    case LearningOutcome::kPending:
      return "Pending";
    case LearningOutcome::kMatchesInstant:
      return "TIME=INSTANT";
    case LearningOutcome::kSingleDistinct:
      return "Single frame";
    case LearningOutcome::kOddEvenDual:
      return "Odd/even dual";
    case LearningOutcome::kInconclusive:
      return "Inconclusive";
  }

  return "?";
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
  printStatus("Waiting for IR", backendLine(), "A:IR  B:reset");
}

void clearAcceptedCaptures() {
  g_acceptedCaptureCount = 0;
  g_learningOutcome = LearningOutcome::kPending;
  for (size_t i = 0; i < kMaxAcceptedCaptures; ++i) {
    g_acceptedCaptures[i].used = false;
    g_acceptedCaptures[i].cluster = CaptureCluster::kNone;
    g_acceptedCaptures[i].captureIndex = 0;
    g_acceptedCaptures[i].symbolCount = 0;
  }
}

void printLearningGuide() {
  Serial.println();
  Serial.println("=== Bulb Learning Guide ===");
  Serial.println("1. Set the camera to B.");
  Serial.println("2. Press only the original TIME button.");
  Serial.println("3. Capture 8-10 raw frames and keep at least 6 accepted frames.");
  Serial.println("4. Accepted frames must keep symbol count, level pattern, and");
  Serial.println("   per-position short/long structure stable.");
  Serial.println("5. If the first accepted frame looks wrong, press BtnB to reset");
  Serial.println("   the learning set and start over.");
  Serial.println("6. Do not promote any bulb frame into replay_profile.h until");
  Serial.println("   empty-camera validation succeeds.");
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

bool isGapDuration(uint16_t duration) {
  return duration >= kGapThresholdUs;
}

bool sameRelation(uint16_t lhs, uint16_t rhs, uint16_t refLhs, uint16_t refRhs) {
  const bool lhsComparable = lhs > 0 && rhs > 0 && !isGapDuration(lhs) && !isGapDuration(rhs);
  const bool refComparable =
      refLhs > 0 && refRhs > 0 && !isGapDuration(refLhs) && !isGapDuration(refRhs);

  if (!lhsComparable || !refComparable) {
    return true;
  }

  const uint32_t lhsDelta = (lhs > rhs) ? (lhs - rhs) : (rhs - lhs);
  const uint32_t refDelta =
      (refLhs > refRhs) ? (refLhs - refRhs) : (refRhs - refLhs);
  const uint32_t lhsMin = (lhs < rhs) ? lhs : rhs;
  const uint32_t refMin = (refLhs < refRhs) ? refLhs : refRhs;
  const bool lhsSimilar = lhsDelta * 100U <= lhsMin * kRelationTolerancePercent;
  const bool refSimilar = refDelta * 100U <= refMin * kRelationTolerancePercent;

  if (lhsSimilar || refSimilar) {
    return lhsSimilar == refSimilar;
  }

  return (lhs > rhs) == (refLhs > refRhs);
}

bool sameStructure(const IrSymbol* lhs, const IrSymbol* rhs, size_t symbolCount) {
  for (size_t i = 0; i < symbolCount; ++i) {
    if (lhs[i].level0 != rhs[i].level0 || lhs[i].level1 != rhs[i].level1) {
      return false;
    }

    const bool lhsZero0 = lhs[i].duration0 == 0;
    const bool lhsZero1 = lhs[i].duration1 == 0;
    const bool rhsZero0 = rhs[i].duration0 == 0;
    const bool rhsZero1 = rhs[i].duration1 == 0;
    if (lhsZero0 != rhsZero0 || lhsZero1 != rhsZero1) {
      return false;
    }

    if (isGapDuration(lhs[i].duration0) != isGapDuration(rhs[i].duration0) ||
        isGapDuration(lhs[i].duration1) != isGapDuration(rhs[i].duration1)) {
      return false;
    }

    if (!sameRelation(lhs[i].duration0,
                      lhs[i].duration1,
                      rhs[i].duration0,
                      rhs[i].duration1)) {
      return false;
    }
  }

  return true;
}

void copyRxSymbols(IrSymbol* target, size_t symbolCount) {
  for (size_t i = 0; i < symbolCount; ++i) {
    target[i].duration0 = g_rxSymbols[i].duration0;
    target[i].duration1 = g_rxSymbols[i].duration1;
    target[i].level0 = g_rxSymbols[i].level0;
    target[i].level1 = g_rxSymbols[i].level1;
  }
}

bool matchesStructure(
    const IrSymbol* lhs,
    size_t lhsCount,
    const IrSymbol* rhs,
    size_t rhsCount
) {
  if (lhsCount != rhsCount) {
    return false;
  }

  return sameStructure(lhs, rhs, lhsCount);
}

const StoredCapture* firstCaptureForCluster(CaptureCluster cluster) {
  for (size_t i = 0; i < g_acceptedCaptureCount; ++i) {
    if (g_acceptedCaptures[i].used && g_acceptedCaptures[i].cluster == cluster) {
      return &g_acceptedCaptures[i];
    }
  }

  return nullptr;
}

CaptureCluster classifyCapture(const IrSymbol* symbols, size_t symbolCount) {
  const StoredCapture* clusterA = firstCaptureForCluster(CaptureCluster::kA);
  if (clusterA == nullptr) {
    return CaptureCluster::kA;
  }

  if (matchesStructure(symbols,
                       symbolCount,
                       clusterA->symbols,
                       clusterA->symbolCount)) {
    return CaptureCluster::kA;
  }

  const StoredCapture* clusterB = firstCaptureForCluster(CaptureCluster::kB);
  if (clusterB == nullptr) {
    return CaptureCluster::kB;
  }

  if (matchesStructure(symbols,
                       symbolCount,
                       clusterB->symbols,
                       clusterB->symbolCount)) {
    return CaptureCluster::kB;
  }

  return CaptureCluster::kNone;
}

void recomputeOutcome() {
  if (g_acceptedCaptureCount == 0) {
    g_learningOutcome = LearningOutcome::kPending;
    return;
  }

  bool allMatchInstant = true;
  for (size_t i = 0; i < g_acceptedCaptureCount; ++i) {
    if (!matchesStructure(g_acceptedCaptures[i].symbols,
                          g_acceptedCaptures[i].symbolCount,
                          replay_profile::kInstantSymbols,
                          replay_profile::kInstantSymbolCount)) {
      allMatchInstant = false;
      break;
    }
  }

  if (allMatchInstant) {
    g_learningOutcome = (g_acceptedCaptureCount >= 2)
        ? LearningOutcome::kMatchesInstant
        : LearningOutcome::kPending;
    return;
  }

  const StoredCapture* clusterB = firstCaptureForCluster(CaptureCluster::kB);
  if (clusterB == nullptr) {
    g_learningOutcome = (g_acceptedCaptureCount >= 2)
        ? LearningOutcome::kSingleDistinct
        : LearningOutcome::kPending;
    return;
  }

  if (g_acceptedCaptureCount < 4) {
    g_learningOutcome = LearningOutcome::kPending;
    return;
  }

  const CaptureCluster evenCluster = g_acceptedCaptures[0].cluster;
  const CaptureCluster oddCluster = g_acceptedCaptures[1].cluster;
  if (evenCluster == oddCluster) {
    g_learningOutcome = LearningOutcome::kInconclusive;
    return;
  }

  for (size_t i = 0; i < g_acceptedCaptureCount; ++i) {
    const CaptureCluster expected = (i % 2 == 0) ? evenCluster : oddCluster;
    if (g_acceptedCaptures[i].cluster != expected) {
      g_learningOutcome = LearningOutcome::kInconclusive;
      return;
    }
  }

  g_learningOutcome = LearningOutcome::kOddEvenDual;
}

void printCaptureError(uint32_t captureIndex,
                       size_t symbolCount,
                       CaptureValidation validation) {
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

void printSymbolsAsArray(const IrSymbol* symbols,
                         uint32_t captureIndex,
                         size_t symbolCount) {
  Serial.printf("static const IrSymbol kLearnedCapture%lu[] = {\n",
                static_cast<unsigned long>(captureIndex));

  for (size_t i = 0; i < symbolCount; ++i) {
    const bool isLastSymbol = (i + 1 == symbolCount);
    Serial.printf(
        "  {%u, %u, %u, %u}%s\n",
        static_cast<unsigned>(symbols[i].duration0),
        static_cast<unsigned>(symbols[i].duration1),
        static_cast<unsigned>(symbols[i].level0),
        static_cast<unsigned>(symbols[i].level1),
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

void printAcceptedSequence() {
  Serial.print("Accepted sequence:");
  if (g_acceptedCaptureCount == 0) {
    Serial.println(" (none)");
    return;
  }

  for (size_t i = 0; i < g_acceptedCaptureCount; ++i) {
    Serial.print(' ');
    Serial.print(clusterLabel(g_acceptedCaptures[i].cluster));
  }
  Serial.println();
}

void printLearningSummary(const IrSymbol* symbols,
                          uint32_t captureIndex,
                          size_t symbolCount,
                          CaptureCluster cluster) {
  Serial.println();
  Serial.printf("=== Capture %lu ===\n", static_cast<unsigned long>(captureIndex));
  Serial.printf("Symbol count: %u\n", static_cast<unsigned>(symbolCount));
  Serial.printf("Accepted as cluster: %s\n", clusterLabel(cluster));
  printSymbolsAsArray(symbols, captureIndex, symbolCount);

  const bool matchesInstant = matchesStructure(symbols,
                                               symbolCount,
                                               replay_profile::kInstantSymbols,
                                               replay_profile::kInstantSymbolCount);
  Serial.printf("Matches instant structure: %s\n", matchesInstant ? "yes" : "no");
  Serial.printf("Accepted captures: %u/%u\n",
                static_cast<unsigned>(g_acceptedCaptureCount),
                static_cast<unsigned>(kMaxAcceptedCaptures));
  printAcceptedSequence();
  Serial.printf("Current outcome: %s\n", outcomeLabel(g_learningOutcome));

  if (g_learningOutcome == LearningOutcome::kPending) {
    Serial.println("Need more accepted captures before promotion.");
  } else if (g_learningOutcome == LearningOutcome::kInconclusive) {
    Serial.println("Odd/even clustering is unstable. Reset with BtnB and resample.");
  } else {
    Serial.println("Still validate on the real camera body before promotion.");
  }
}

void printStructuralReject(uint32_t captureIndex, size_t symbolCount) {
  Serial.println();
  Serial.printf("=== Capture %lu ===\n", static_cast<unsigned long>(captureIndex));
  Serial.printf("Symbol count: %u\n", static_cast<unsigned>(symbolCount));
  Serial.println("Valid raw frame, but rejected from accepted set.");
  Serial.println("Reason: introduced a third structural shape or broke the");
  Serial.println("existing odd/even clustering. Reset with BtnB and retry if");
  Serial.println("the first accepted capture was likely an outlier.");
}

bool storeAcceptedCapture(const IrSymbol* symbols,
                          uint32_t captureIndex,
                          size_t symbolCount,
                          CaptureCluster cluster) {
  if (cluster == CaptureCluster::kNone ||
      g_acceptedCaptureCount >= kMaxAcceptedCaptures) {
    return false;
  }

  StoredCapture& stored = g_acceptedCaptures[g_acceptedCaptureCount];
  stored.used = true;
  stored.cluster = cluster;
  stored.captureIndex = captureIndex;
  stored.symbolCount = symbolCount;
  for (size_t i = 0; i < symbolCount; ++i) {
    stored.symbols[i].duration0 = symbols[i].duration0;
    stored.symbols[i].duration1 = symbols[i].duration1;
    stored.symbols[i].level0 = symbols[i].level0;
    stored.symbols[i].level1 = symbols[i].level1;
  }

  ++g_acceptedCaptureCount;
  recomputeOutcome();
  return true;
}

void renderCaptureSummary(size_t symbolCount,
                          CaptureValidation validation,
                          bool accepted,
                          CaptureCluster cluster) {
  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.println("Lomo IR Capture");
  M5.Display.println();
  M5.Display.printf("Raw:%lu Acc:%u\n",
                    static_cast<unsigned long>(g_captureCount),
                    static_cast<unsigned>(g_acceptedCaptureCount));
  M5.Display.printf("Sym:%u\n", static_cast<unsigned>(symbolCount));
  M5.Display.println(backendLine());

  if (validation != CaptureValidation::kOk) {
    switch (validation) {
      case CaptureValidation::kEmpty:
        M5.Display.println("Rejected: empty");
        break;
      case CaptureValidation::kTooShort:
        M5.Display.println("Rejected: noise");
        break;
      case CaptureValidation::kTooLong:
        M5.Display.println("Rejected: too long");
        break;
      case CaptureValidation::kOk:
        break;
    }
  } else if (!accepted) {
    M5.Display.println("Rejected: shape");
  } else {
    M5.Display.printf("Acc:%s\n", clusterLabel(cluster));
  }

  M5.Display.printf("Out:%s\n", outcomeLabel(g_learningOutcome));
  M5.Display.println("See Serial");
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
  clearAcceptedCaptures();
  showWaitingStatus();
  printLearningGuide();

  setupReceiver();
  beginReceive();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasClicked()) {
    switchBackend(nextIrBackend(g_backend));
  }

  if (M5.BtnB.wasPressed()) {
    clearAcceptedCaptures();
    Serial.println();
    Serial.println("Accepted capture set reset.");
    showWaitingStatus();
  }

  if (!g_rxDone) {
    delay(10);
    return;
  }

  g_rxDone = false;
  const uint32_t captureIndex = ++g_captureCount;
  const CaptureValidation validation = validateCapture(g_rxSymbolCount);
  bool accepted = false;
  CaptureCluster cluster = CaptureCluster::kNone;

  if (validation == CaptureValidation::kOk) {
    IrSymbol receivedSymbols[kMaxReplaySymbols];
    copyRxSymbols(receivedSymbols, g_rxSymbolCount);
    cluster = classifyCapture(receivedSymbols, g_rxSymbolCount);
    accepted = storeAcceptedCapture(receivedSymbols,
                                    captureIndex,
                                    g_rxSymbolCount,
                                    cluster);
    if (accepted) {
      printLearningSummary(receivedSymbols,
                           captureIndex,
                           g_rxSymbolCount,
                           cluster);
    } else {
      printStructuralReject(captureIndex, g_rxSymbolCount);
    }
  } else {
    printCaptureError(captureIndex, g_rxSymbolCount, validation);
  }

  renderCaptureSummary(g_rxSymbolCount, validation, accepted, cluster);
  delay(250);
  showWaitingStatus();
  beginReceive();
}
