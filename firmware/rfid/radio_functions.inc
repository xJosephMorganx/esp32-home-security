void increment(uint32_t& counter) {
  portENTER_CRITICAL(&countersMux); ++counter; portEXIT_CRITICAL(&countersMux);
}

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

void printMac(const uint8_t* mac) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool allowed(uint8_t value) {
  for (uint8_t c : RfidConfig::kChannels) if (c == value) return true;
  return false;
}

bool nonzero(const uint8_t* bytes, size_t length) {
  for (size_t i = 0; i < length; ++i) if (bytes[i]) return true;
  return false;
}

bool configurationValid() {
  if (!RfidConfig::kProvisioned || (RfidConfig::kHubStaMac[0] & 1)
      || !nonzero(RfidConfig::kHubStaMac, 6)
      || memcmp(staMac, RfidConfig::kHubStaMac, 6) == 0
      || memcmp(staMac, RfidConfig::kRfidStaMac, 6) != 0
      || !nonzero(RfidConfig::kPmk, 16) || !nonzero(RfidConfig::kLmk, 16)
      || !allowed(RfidConfig::kFallbackChannel)
      || RfidConfig::kCountryFirstChannel < 1 || RfidConfig::kCountryChannelCount == 0
      || unsigned(RfidConfig::kCountryFirstChannel) + RfidConfig::kCountryChannelCount > 15)
    return false;
  for (size_t i = 0; i < kChannelCount; ++i) {
    const uint8_t c = RfidConfig::kChannels[i];
    if (c < 1 || c > 14 || c < RfidConfig::kCountryFirstChannel
        || unsigned(c) >= unsigned(RfidConfig::kCountryFirstChannel) + RfidConfig::kCountryChannelCount)
      return false;
    for (size_t j = 0; j < i; ++j) if (c == RfidConfig::kChannels[j]) return false;
  }
  return true;
}

bool checked(esp_err_t result, const char* operation) {
  if (result == ESP_OK) return true;
  Serial.printf("%s error=%s\n", operation, esp_err_to_name(result));
  fault(operation); return false;
}

bool setChannel(uint8_t target) {
  if (inFlight || (link.heartbeat.active || link.card.active)) { fault("channel change while busy"); return false; }
  if (!checked(esp_wifi_set_channel(target, WIFI_SECOND_CHAN_NONE), "set channel")) return false;
  uint8_t actual = 0; wifi_second_chan_t secondary;
  if (!checked(esp_wifi_get_channel(&actual, &secondary), "read channel")) return false;
  if (actual != target) { fault("channel verification"); return false; }
  channel = actual;
  for (size_t i = 0; i < kChannelCount; ++i) if (RfidConfig::kChannels[i] == channel) channelIndex = i;
  needHello = true;
  Serial.printf("SEARCH channel=%u\n", channel); return true;
}

bool startSession() {
  if (nvs_open("rfid-v1", NVS_READWRITE, &nvsHandle) != ESP_OK) return false;
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
  memcpy(country.cc, RfidConfig::kCountryCode, sizeof(country.cc));
  country.schan = RfidConfig::kCountryFirstChannel;
  country.nchan = RfidConfig::kCountryChannelCount;
  country.max_tx_power = 20; country.policy = WIFI_COUNTRY_POLICY_MANUAL;
  if (!checked(esp_wifi_set_country(&country), "regulatory domain")) return;
  if (!checked(esp_now_init(), "ESP-NOW init")) return;
  radioInitialized = true;
  if (!checked(esp_now_set_pmk(RfidConfig::kPmk), "set PMK")) return;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, RfidConfig::kHubStaMac, 6); memcpy(peer.lmk, RfidConfig::kLmk, 16);
  peer.ifidx = WIFI_IF_STA; peer.channel = 0; peer.encrypt = true;
  if (!checked(esp_now_add_peer(&peer), "encrypted STA peer")) return;
  if (!checked(esp_now_register_recv_cb(onReceive), "RX callback")
      || !checked(esp_now_register_send_cb(onSend), "TX callback")) return;
  transport = true;
  if (!setChannel(allowed(savedChannel) ? savedChannel : RfidConfig::kFallbackChannel)) return;
  Serial.print("Hub STA peer="); printMac(RfidConfig::kHubStaMac); Serial.println(" encrypted unicast");
}

void completeSend(uint32_t now) {
  TxItem item{};
  if (xQueueReceive(txQueue, &item, 0) == pdTRUE) {
    if (!inFlight || memcmp(item.destination, RfidConfig::kHubStaMac, 6) != 0) {
      fault("unexpected TX callback"); return;
    }
    inFlight = false;
    if (item.status != ESP_NOW_SEND_SUCCESS) ++radioFailures;
    Serial.printf("TX MAC=%s (not application ACK)\n", item.status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
  }
  if (inFlight && elapsed(now, sendStarted, kSendCallbackTimeoutMs)) fault("TX callback timeout");
}
