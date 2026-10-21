#include <Arduino.h>
#include <WiFi.h>
#include <nvs.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "pir_logic.h"
#include "pir_link.h"
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

namespace {
using namespace Domotica;
constexpr uint8_t kSensorPin = 27;
constexpr uint8_t kLedPin = 25;
constexpr uint32_t kSendCallbackTimeoutMs = 1000;
constexpr size_t kChannelCount = sizeof(PirConfig::kChannels);
static_assert(kChannelCount > 0 && kChannelCount <= 14, "Provide 1..14 legal channels");
static_assert(sizeof(PirConfig::kHubStaMac) == 6 && sizeof(PirConfig::kPirStaMac) == 6
              && sizeof(PirConfig::kPmk) == 16
              && sizeof(PirConfig::kLmk) == 16, "Invalid provisioning field lengths");
static_assert(PirConfig::kSampleMs > 0 && PirConfig::kSampleMs < 0x80000000UL
              && PirConfig::kWarmupMs < 0x80000000UL && PirConfig::kFilterMs < 0x80000000UL,
              "Invalid sensor timer interval");
constexpr bool kSensorConfigured = PirConfig::kSensorConfigurationConfirmed
    && PirConfig::kMotionLevel != PirConfig::Level::Unspecified && PirConfig::kWarmupMs > 0;

struct RxItem {
  uint8_t source[6], destination[6], bytes[kMaxFrameSize];
  uint8_t length, channel;
  uint32_t at;
};
struct TxItem { uint8_t destination[6]; esp_now_send_status_t status; };
QueueHandle_t rxQueue = nullptr, txQueue = nullptr;
StaticQueue_t rxQueueControl, txQueueControl;
uint8_t rxStorage[kRxQueueDepth * sizeof(RxItem)], txStorage[sizeof(TxItem)];
portMUX_TYPE countersMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t rxDrops = 0, callbackRejects = 0, txDrops = 0;

nvs_handle_t nvsHandle = 0;
Pir::Sensor sensor;
Pir::Events events;
Pir::Pending pending;
Pir::Link hubLink;
uint8_t staMac[6]{};
uint32_t boot = 0, sequence = 0, lastHeartbeat = 0, dwellStarted = 0;
uint32_t sendStarted = 0, lastDiagnostic = 0;
uint32_t rejected = 0, failedReports = 0, sendErrors = 0, radioFailures = 0;
uint8_t channel = 0, savedChannel = 0;
size_t channelIndex = 0;
bool transport = false, radioInitialized = false, inFlight = false;
bool searching = true, needHello = true, led = false;
uint32_t lastSample = 0;
bool sampled = false;

void writeLed(bool on) {
  if (PirConfig::kLedActiveLevel == PirConfig::Level::Unspecified) return;
  digitalWrite(kLedPin, on == (PirConfig::kLedActiveLevel == PirConfig::Level::High) ? HIGH : LOW);
}

void increment(uint32_t& counter) {
  portENTER_CRITICAL(&countersMux); ++counter; portEXIT_CRITICAL(&countersMux);
}

// Wi-Fi task callbacks: bounded copies only; no Serial, NVS or protocol work.
void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
  if (!info || !info->src_addr || !info->des_addr || !info->rx_ctrl || !data
      || length <= 0 || length > int(kMaxFrameSize)) {
    increment(callbackRejects); return;
  }
  RxItem item{};
  memcpy(item.source, info->src_addr, 6); memcpy(item.destination, info->des_addr, 6);
  memcpy(item.bytes, data, length); item.length = uint8_t(length);
  item.channel = info->rx_ctrl->channel; item.at = millis();
  if (xQueueSend(rxQueue, &item, 0) != pdTRUE) increment(rxDrops);
}

void onSend(const esp_now_send_info_t* info, esp_now_send_status_t status) {
  if (!info || !info->des_addr) { increment(callbackRejects); return; }
  TxItem item{}; memcpy(item.destination, info->des_addr, 6); item.status = status;
  if (xQueueSend(txQueue, &item, 0) != pdTRUE) increment(txDrops);
}

const char* sensorName(PirState state) {
  switch (state) {
    case PirState::NotReady: return "NOT_READY";
    case PirState::NoMotion: return "NO_MOTION";
    case PirState::Motion: return "MOTION";
    default: return "UNKNOWN";
  }
}
PirState snapshot() {
  return sensor.wireState(kSensorConfigured, PirConfig::kMotionLevel == PirConfig::Level::High);
}
void printMac(const uint8_t* mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
bool allowed(uint8_t value) {
  for (uint8_t c : PirConfig::kChannels) if (c == value) return true;
  return false;
}
bool nonzero(const uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; ++i) if (bytes[i]) return true;
  return false;
}
bool configurationValid() {
  if (!PirConfig::kProvisioned || (PirConfig::kHubStaMac[0] & 1)
      || !nonzero(PirConfig::kHubStaMac, 6)
      || memcmp(staMac, PirConfig::kHubStaMac, 6) == 0
      || memcmp(staMac, PirConfig::kPirStaMac, 6) != 0
      || !nonzero(PirConfig::kPmk, 16) || !nonzero(PirConfig::kLmk, 16)
      || !allowed(PirConfig::kFallbackChannel)
      || PirConfig::kCountryFirstChannel < 1 || PirConfig::kCountryChannelCount == 0
      || unsigned(PirConfig::kCountryFirstChannel) + PirConfig::kCountryChannelCount > 15)
    return false;
  for (size_t i = 0; i < kChannelCount; ++i) {
    const uint8_t c = PirConfig::kChannels[i];
    if (c < 1 || c > 14 || c < PirConfig::kCountryFirstChannel
        || unsigned(c) >= unsigned(PirConfig::kCountryFirstChannel) + PirConfig::kCountryChannelCount)
      return false;
    for (size_t j = 0; j < i; ++j) if (c == PirConfig::kChannels[j]) return false;
  }
  return true;
}

void fault(const char* reason) {
  // Fail closed. In particular, NEVER clear an outstanding send and start another
  // after a missing callback. A reset is required for this exceptional driver fault.
  transport = false; pending.active = false; events.clear();
  led = false; writeLed(false);
  Serial.printf("TRANSPORT DISABLED: %s; sensor remains active; reset after correction\n", reason);
}
bool checked(esp_err_t result, const char* operation) {
  if (result == ESP_OK) return true;
  Serial.printf("%s error=%s\n", operation, esp_err_to_name(result));
  fault(operation); return false;
}
bool setChannel(uint8_t target) {
  if (inFlight || pending.active) { fault("channel change while busy"); return false; }
  if (!checked(esp_wifi_set_channel(target, WIFI_SECOND_CHAN_NONE), "set channel")) return false;
  uint8_t actual = 0; wifi_second_chan_t secondary;
  if (!checked(esp_wifi_get_channel(&actual, &secondary), "read channel")) return false;
  if (actual != target) { fault("channel verification"); return false; }
  channel = actual;
  for (size_t i = 0; i < kChannelCount; ++i) if (PirConfig::kChannels[i] == channel) channelIndex = i;
  needHello = true;
  Serial.printf("SEARCH channel=%u\n", channel); return true;
}

bool startSession() {
  if (nvs_open("pir-v1", NVS_READWRITE, &nvsHandle) != ESP_OK) return false;
  uint32_t previous = 0;
  const esp_err_t read = nvs_get_u32(nvsHandle, "boot", &previous);
  if (read != ESP_OK && read != ESP_ERR_NVS_NOT_FOUND) return false;
  boot = nextCounter(previous);
  uint32_t verified = 0;
  if (nvs_set_u32(nvsHandle, "boot", boot) != ESP_OK || nvs_commit(nvsHandle) != ESP_OK
      || nvs_get_u32(nvsHandle, "boot", &verified) != ESP_OK || verified != boot) return false;
  if (nvs_get_u8(nvsHandle, "channel", &savedChannel) != ESP_OK) savedChannel = 0;
  Serial.printf("NVS session=%lu (persisted before TX)\n", (unsigned long)boot);
  return true;
}

void beginRadio() {
  if (!configurationValid()) { fault("missing/invalid local provisioning or channels"); return; }
  wifi_country_t country{};
  memcpy(country.cc, PirConfig::kCountryCode, sizeof(country.cc));
  country.schan = PirConfig::kCountryFirstChannel;
  country.nchan = PirConfig::kCountryChannelCount;
  country.max_tx_power = 20; country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  if (!checked(esp_wifi_set_country(&country), "regulatory domain")) return;
  if (!checked(esp_now_init(), "ESP-NOW init")) return;
  radioInitialized = true;
  if (!checked(esp_now_set_pmk(PirConfig::kPmk), "set PMK")) return;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, PirConfig::kHubStaMac, 6); memcpy(peer.lmk, PirConfig::kLmk, 16);
  peer.ifidx = WIFI_IF_STA; peer.channel = 0; peer.encrypt = true;
  if (!checked(esp_now_add_peer(&peer), "encrypted STA peer")) return;
  if (!checked(esp_now_register_recv_cb(onReceive), "RX callback")
      || !checked(esp_now_register_send_cb(onSend), "TX callback")) return;
  transport = true;
  if (!setChannel(allowed(savedChannel) ? savedChannel : PirConfig::kFallbackChannel)) return;
  Serial.print("Hub STA peer="); printMac(PirConfig::kHubStaMac); Serial.println(" encrypted unicast");
}

void startReport(Type type, PirState state, uint32_t now) {
  sequence = nextCounter(sequence);
  if (!pending.start(type, state, {boot, sequence}, now)) { fault("codec refused report"); return; }
  Serial.printf("REPORT type=%u session=%lu seq=%lu sensor=%s\n", unsigned(type),
                (unsigned long)boot, (unsigned long)sequence, sensorName(state));
}

void receive(uint32_t now) {
  RxItem item{};
  // Bound work even if the Wi-Fi task keeps filling the queue.
  for (size_t i = 0; i < kRxQueueDepth && xQueueReceive(rxQueue, &item, 0) == pdTRUE; ++i) {
    now = millis(); // Callback timestamp may be newer than the loop's original time.
    Frame frame{};
    const bool wasPending = pending.active;
    if (memcmp(item.source, PirConfig::kHubStaMac, 6) != 0
        || memcmp(item.destination, staMac, 6) != 0
        || elapsed(now, item.at, kOfflineMs)
        || decode(item.bytes, item.length, frame) != Error::Ok
        || !hubLink.accept(frame, pending, channel, item.channel, item.at)) {
      ++rejected; continue;
    }
    if (wasPending && !pending.active) {
      Serial.printf("ACK application session=%lu seq=%lu\n",
                    (unsigned long)hubLink.confirmed.boot, (unsigned long)hubLink.confirmed.sequence);
    }
    Serial.printf("SYNC hub=%lu/%lu global=%u revision=%lu channel=%u\n",
                  (unsigned long)frame.stamp.boot, (unsigned long)frame.stamp.sequence,
                  unsigned(hubLink.system), (unsigned long)hubLink.revision, channel);
    if (searching) {
      searching = false; needHello = false; lastHeartbeat = now;
      // A fresh periodic sync may reference our last confirmed report, rather
      // than the HELLO now pending. Publish the current snapshot on recovery too.
      events.heartbeat(snapshot());
      Serial.println("HUB ONLINE (fresh correlated STATE_SYNC)");
      if (savedChannel != channel) {
        if (nvs_set_u8(nvsHandle, "channel", channel) == ESP_OK && nvs_commit(nvsHandle) == ESP_OK)
          savedChannel = channel;
        else Serial.println("NVS channel save failed; reboot will use previous/fallback channel");
      }
    }
  }
}

void completeSend(uint32_t now) {
  TxItem item{};
  if (xQueueReceive(txQueue, &item, 0) == pdTRUE) {
    if (!inFlight || memcmp(item.destination, PirConfig::kHubStaMac, 6) != 0) {
      fault("unexpected TX callback"); return;
    }
    inFlight = false;
    if (item.status != ESP_NOW_SEND_SUCCESS) ++radioFailures;
    Serial.printf("TX MAC=%s (not application ACK)\n", item.status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
  }
  if (inFlight && elapsed(now, sendStarted, kSendCallbackTimeoutMs)) fault("TX callback timeout");
}

void schedule(uint32_t now) {
  if (!searching && !hubLink.hub.online(now)) {
    searching = true; needHello = true; events.clear();
    // Keep the pending send until ACK/deadline and driver completion; retain tracker.
    Serial.println("HUB OFFLINE: 15s without fresh valid sync; searching current channel first");
  }
  if (pending.expired(now)) {
    pending.active = false; ++failedReports;
    Serial.printf("REPORT LOST seq=%lu after 750ms\n", (unsigned long)pending.stamp.sequence);
  }
  if (!searching && !events.heartbeatReady && elapsed(now, lastHeartbeat, kHeartbeatMs)) {
    lastHeartbeat = now; events.heartbeat(snapshot());
  }
  // Warmup and unknown configuration must NEVER delay HELLO/STATUS or channel search.
  if (!pending.active && !inFlight) {
    if (searching) {
      if (!needHello && elapsed(now, dwellStarted, kChannelDwellMs)) {
        if (!setChannel(PirConfig::kChannels[(channelIndex + 1) % kChannelCount])) return;
      }
      if (needHello) {
        events.clear(); needHello = false; dwellStarted = now;
        startReport(Type::PirHello, snapshot(), now);
      }
    } else {
      Type type; PirState state;
      if (events.take(type, state)) startReport(type, state, now);
    }
  }
  if (transport && !inFlight && pending.due(now)) {
    // Bytes remain unchanged across retries. The driver copies the buffer on send.
    ++pending.attempts; inFlight = true; sendStarted = now;
    const esp_err_t result = esp_now_send(PirConfig::kHubStaMac, pending.bytes, pending.length);
    Serial.printf("TX seq=%lu attempt=%u channel=%u\n", (unsigned long)pending.stamp.sequence,
                  pending.attempts, channel);
    if (result != ESP_OK) {
      inFlight = false; ++sendErrors;
      Serial.printf("TX submission failed: %s\n", esp_err_to_name(result));
    }
  }
}

void diagnostics(uint32_t now) {
  if (!elapsed(now, lastDiagnostic, kHeartbeatMs)) return;
  lastDiagnostic = now;
  uint32_t drops, bad, txLost;
  portENTER_CRITICAL(&countersMux);
  drops = rxDrops; bad = callbackRejects; txLost = txDrops;
  portEXIT_CRITICAL(&countersMux);
  Serial.printf("DIAG transport=%s raw=%u stable=%s sensor=%s channel=%u session=%lu online=%u LED=%u "
                "rxAgeMs=%lu queued=%u changesLost=%lu reportsLost=%lu rxDropped=%lu rejected=%lu "
                "callbackBad=%lu txDropped=%lu submitErrors=%lu macFailures=%lu\n",
                transport ? (searching ? "SEARCH" : "LINKED") : "DISABLED",
                digitalRead(kSensorPin), sensor.ready ? (sensor.stable ? "HIGH" : "LOW") : "UNSTABLE",
                sensorName(snapshot()), channel, (unsigned long)boot,
                transport && !searching && hubLink.hub.online(now), led,
                (unsigned long)(hubLink.hub.seen ? uint32_t(now - hubLink.hub.receivedAt) : UINT32_MAX),
                unsigned(events.count), (unsigned long)events.lost, (unsigned long)failedReports,
                (unsigned long)drops, (unsigned long)rejected, (unsigned long)bad, (unsigned long)txLost,
                (unsigned long)sendErrors, (unsigned long)radioFailures);
}
} // namespace

void setup() {
  if (PirConfig::kLedActiveLevel == PirConfig::Level::Unspecified) pinMode(kLedPin, INPUT);
  else { writeLed(false); pinMode(kLedPin, OUTPUT); }
  pinMode(kSensorPin, INPUT);
  Serial.begin(115200);
  Serial.printf("\nPIR protocol v1 / library 1.1.0 | Arduino-ESP32 %s | PIR GPIO27 INPUT | LED GPIO25\n",
                ESP_ARDUINO_VERSION_STR);
  Serial.printf("Sensor configuration=%s warmup=%lums filter=%lums sample=%lums mode=%s\n",
                kSensorConfigured ? "USER-CONFIGURED" : "UNCONFIRMED (reports UNKNOWN)",
                (unsigned long)PirConfig::kWarmupMs, (unsigned long)PirConfig::kFilterMs,
                (unsigned long)PirConfig::kSampleMs, PirConfig::kSensorPhysicalMode);
  if (PirConfig::kLedActiveLevel == PirConfig::Level::Unspecified)
    Serial.println("LED polarity unspecified: high impedance; physical off state unverified");
  sensor.sample(digitalRead(kSensorPin) == HIGH, millis(), PirConfig::kWarmupMs, PirConfig::kFilterMs);
  rxQueue = xQueueCreateStatic(kRxQueueDepth, sizeof(RxItem), rxStorage, &rxQueueControl);
  txQueue = xQueueCreateStatic(1, sizeof(TxItem), txStorage, &txQueueControl);
  if (!rxQueue || !txQueue) { fault("queue initialization"); return; }
  WiFi.persistent(false); WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_STA)) { fault("STA start"); return; }
  // No WiFi.begin(): PIR never associates/scans. Disconnect any inherited STA association.
  const esp_err_t disconnect = esp_wifi_disconnect();
  if (disconnect != ESP_OK && disconnect != ESP_ERR_WIFI_NOT_CONNECT) {
    checked(disconnect, "STA disconnect"); return;
  }
  if (!checked(esp_wifi_set_ps(WIFI_PS_NONE), "disable Wi-Fi sleep")
      || !checked(esp_wifi_get_mac(WIFI_IF_STA, staMac), "read STA MAC")) return;
  Serial.print("PIR STA MAC="); printMac(staMac); Serial.println();
  if (!configurationValid()) { fault("missing/invalid local provisioning or channels"); return; }
  if (!startSession()) { fault("NVS boot counter persistence"); return; }
  beginRadio();
}

void loop() {
  uint32_t now = millis();
  if (!sampled || elapsed(now, lastSample, PirConfig::kSampleMs)) {
    sampled = true; lastSample = now;
    const PirState previous = snapshot();
    sensor.sample(digitalRead(kSensorPin) == HIGH, now, PirConfig::kWarmupMs, PirConfig::kFilterMs);
    const PirState current = snapshot();
    if (previous != current) {
      Serial.printf("SENSOR %s -> %s\n", sensorName(previous), sensorName(current));
      // Includes NOT_READY -> MOTION/NO_MOTION while a HELLO is pending.
      if (transport) events.push(current);
    }
  }
  if (transport) {
    completeSend(now);
    if (transport) receive(now);
    if (transport) schedule(millis());
  } else if (radioInitialized) {
    // Drain bounded callback queues after a latched fault, but never transmit again.
    RxItem rx{}; TxItem tx{};
    for (size_t i = 0; i < kRxQueueDepth && xQueueReceive(rxQueue, &rx, 0) == pdTRUE; ++i) {}
    xQueueReceive(txQueue, &tx, 0);
  }
  now = millis();
  const bool desired = PirConfig::kLedActiveLevel != PirConfig::Level::Unspecified
      && transport && !searching && hubLink.led(now);
  if (desired != led) { led = desired; writeLed(led); }
  diagnostics(now);
  yield();
}

