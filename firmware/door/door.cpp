#include <Arduino.h>
#include <WiFi.h>
#include <nvs.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "door_logic.h"
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

namespace {
using namespace Domotica;
constexpr uint8_t kSensorPin = 27;
constexpr uint8_t kLedPin = 16;
constexpr uint32_t kSendCallbackTimeoutMs = 1000;
constexpr size_t kChannelCount = sizeof(DoorConfig::kChannels);
static_assert(kChannelCount > 0 && kChannelCount <= 14, "Provide 1..14 legal channels");
static_assert(sizeof(DoorConfig::kHubStaMac) == 6 && sizeof(DoorConfig::kPmk) == 16
              && sizeof(DoorConfig::kLmk) == 16, "Invalid provisioning field lengths");
static_assert(DoorConfig::kDebounceMs > 0 && DoorConfig::kDebounceMs < 0x80000000UL,
              "Debounce must be a positive modular interval");

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
Door::Debouncer sensor;
Door::Events events;
Door::Pending pending;
Door::Link hubLink;
uint8_t staMac[6]{};
uint32_t boot = 0, sequence = 0, lastHeartbeat = 0, dwellStarted = 0;
uint32_t sendStarted = 0, lastDiagnostic = 0;
uint32_t rejected = 0, failedReports = 0, sendErrors = 0, radioFailures = 0;
uint8_t channel = 0, savedChannel = 0;
size_t channelIndex = 0;
bool transport = false, radioInitialized = false, inFlight = false;
bool searching = true, needHello = true, led = false;

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

const char* sensorName(DoorState state) {
  switch (state) {
    case DoorState::Closed: return "CLOSED";
    case DoorState::Open: return "OPEN";
    default: return "UNKNOWN";
  }
}
DoorState snapshot() {
  return sensor.state(DoorConfig::kPolarityVerified, DoorConfig::kClosedLevelHigh);
}
void printMac(const uint8_t* mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
bool allowed(uint8_t value) {
  for (uint8_t c : DoorConfig::kChannels) if (c == value) return true;
  return false;
}
bool nonzero(const uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; ++i) if (bytes[i]) return true;
  return false;
}
bool configurationValid() {
  if (!DoorConfig::kProvisioned || (DoorConfig::kHubStaMac[0] & 1)
      || !nonzero(DoorConfig::kHubStaMac, 6)
      || memcmp(staMac, DoorConfig::kHubStaMac, 6) == 0
      || !nonzero(DoorConfig::kPmk, 16) || !nonzero(DoorConfig::kLmk, 16)
      || !allowed(DoorConfig::kFallbackChannel)) return false;
  for (size_t i = 0; i < kChannelCount; ++i) {
    const uint8_t c = DoorConfig::kChannels[i];
    if (c < 1 || c > 14 || c < DoorConfig::kCountryFirstChannel
        || unsigned(c) >= unsigned(DoorConfig::kCountryFirstChannel) + DoorConfig::kCountryChannelCount)
      return false;
    for (size_t j = 0; j < i; ++j) if (c == DoorConfig::kChannels[j]) return false;
  }
  return true;
}

void fault(const char* reason) {
  // Fail closed. In particular, NEVER clear an outstanding send and start another
  // after a missing callback. A reset is required for this exceptional driver fault.
  transport = false; pending.active = false; events.clear();
  led = false; digitalWrite(kLedPin, LOW);
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
  for (size_t i = 0; i < kChannelCount; ++i) if (DoorConfig::kChannels[i] == channel) channelIndex = i;
  needHello = true;
  Serial.printf("SEARCH channel=%u\n", channel); return true;
}

bool startSession() {
  if (nvs_open("door-v1", NVS_READWRITE, &nvsHandle) != ESP_OK) return false;
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
  memcpy(country.cc, DoorConfig::kCountryCode, sizeof(country.cc));
  country.schan = DoorConfig::kCountryFirstChannel;
  country.nchan = DoorConfig::kCountryChannelCount;
  country.max_tx_power = 20; country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  if (!checked(esp_wifi_set_country(&country), "regulatory domain")) return;
  if (!checked(esp_now_init(), "ESP-NOW init")) return;
  radioInitialized = true;
  if (!checked(esp_now_set_pmk(DoorConfig::kPmk), "set PMK")) return;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, DoorConfig::kHubStaMac, 6); memcpy(peer.lmk, DoorConfig::kLmk, 16);
  peer.ifidx = WIFI_IF_STA; peer.channel = 0; peer.encrypt = true;
  if (!checked(esp_now_add_peer(&peer), "encrypted STA peer")) return;
  if (!checked(esp_now_register_recv_cb(onReceive), "RX callback")
      || !checked(esp_now_register_send_cb(onSend), "TX callback")) return;
  transport = true;
  if (!setChannel(allowed(savedChannel) ? savedChannel : DoorConfig::kFallbackChannel)) return;
  Serial.print("Hub STA peer="); printMac(DoorConfig::kHubStaMac); Serial.println(" encrypted unicast");
}

void startReport(Type type, DoorState state, uint32_t now) {
  sequence = nextCounter(sequence);
  if (!pending.start(type, state, {boot, sequence}, now)) { fault("codec refused report"); return; }
  Serial.printf("REPORT type=%u session=%lu seq=%lu sensor=%s\n", unsigned(type),
                (unsigned long)boot, (unsigned long)sequence, sensorName(state));
}

void receive(uint32_t now) {
  RxItem item{};
  // Bound work even if the Wi-Fi task keeps filling the queue.
  for (size_t i = 0; i < kRxQueueDepth && xQueueReceive(rxQueue, &item, 0) == pdTRUE; ++i) {
    Frame frame{};
    const bool wasPending = pending.active;
    if (memcmp(item.source, DoorConfig::kHubStaMac, 6) != 0
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
    if (!inFlight || memcmp(item.destination, DoorConfig::kHubStaMac, 6) != 0) {
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
  if (!pending.active && !inFlight && sensor.ready) {
    if (searching) {
      if (!needHello && elapsed(now, dwellStarted, kChannelDwellMs)) {
        if (!setChannel(DoorConfig::kChannels[(channelIndex + 1) % kChannelCount])) return;
      }
      if (needHello) {
        events.clear(); needHello = false; dwellStarted = now;
        startReport(Type::Hello, snapshot(), now);
      }
    } else {
      Type type; DoorState state;
      if (events.take(type, state)) startReport(type, state, now);
    }
  }
  if (transport && !inFlight && pending.due(now)) {
    // Bytes remain unchanged across retries. The driver copies the buffer on send.
    ++pending.attempts; inFlight = true; sendStarted = now;
    const esp_err_t result = esp_now_send(DoorConfig::kHubStaMac, pending.bytes, pending.length);
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
  pinMode(kLedPin, OUTPUT); digitalWrite(kLedPin, LOW);
  pinMode(kSensorPin, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.printf("\nDoor v1 | Arduino-ESP32 %s | MC-38 GPIO27 INPUT_PULLUP | LED GPIO16\n", ESP_ARDUINO_VERSION_STR);
  Serial.printf("Polarity=%s; candidate closed level=%s; debounce=%lums\n",
                DoorConfig::kPolarityVerified ? "USER-CONFIGURED" : "UNVERIFIED (reports UNKNOWN)",
                DoorConfig::kClosedLevelHigh ? "HIGH" : "LOW", (unsigned long)DoorConfig::kDebounceMs);
  rxQueue = xQueueCreateStatic(kRxQueueDepth, sizeof(RxItem), rxStorage, &rxQueueControl);
  txQueue = xQueueCreateStatic(1, sizeof(TxItem), txStorage, &txQueueControl);
  WiFi.persistent(false); WiFi.setAutoReconnect(false);
  if (!WiFi.mode(WIFI_STA)) { fault("STA start"); return; }
  // No WiFi.begin(): the door never associates/scans. Disconnect any inherited STA association.
  const esp_err_t disconnect = esp_wifi_disconnect();
  if (disconnect != ESP_OK && disconnect != ESP_ERR_WIFI_NOT_CONNECT) {
    checked(disconnect, "STA disconnect"); return;
  }
  if (!checked(esp_wifi_set_ps(WIFI_PS_NONE), "disable Wi-Fi sleep")
      || !checked(esp_wifi_get_mac(WIFI_IF_STA, staMac), "read STA MAC")) return;
  Serial.print("Door STA MAC="); printMac(staMac); Serial.println();
  if (!startSession()) { fault("NVS boot counter persistence"); return; }
  beginRadio();
}

void loop() {
  const uint32_t now = millis();
  const bool hadInitial = sensor.ready;
  if (sensor.sample(digitalRead(kSensorPin) == HIGH, now, DoorConfig::kDebounceMs)) {
    Serial.printf("SENSOR %s stable=%s mapped=%s\n", hadInitial ? "CHANGE" : "INITIAL",
                  sensor.stable ? "HIGH" : "LOW", sensorName(snapshot()));
    if (transport && hadInitial) events.push(snapshot());
  }
  if (transport) {
    completeSend(now);
    if (transport) receive(now);
    if (transport) schedule(now);
  } else if (radioInitialized) {
    // Drain bounded callback queues after a latched fault, but never transmit again.
    RxItem rx{}; TxItem tx{};
    for (size_t i = 0; i < kRxQueueDepth && xQueueReceive(rxQueue, &rx, 0) == pdTRUE; ++i) {}
    xQueueReceive(txQueue, &tx, 0);
  }
  const bool desired = transport && !searching && hubLink.led(now);
  if (desired != led) { led = desired; digitalWrite(kLedPin, led ? HIGH : LOW); }
  diagnostics(now);
  yield();
}

