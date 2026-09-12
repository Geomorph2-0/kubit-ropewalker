#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "link.h"
#include "config.h"
#include "control.h"
#include "protocol.h"

namespace {

uint16_t s_seq        = 0;
uint32_t s_lastSendMs = 0;
uint32_t s_lastOkMs   = 0;   // 0 until the first successful send callback
uint32_t s_sent       = 0;
uint32_t s_fail       = 0;

bool s_pendingCapture = false;
bool s_pendingZero    = false;

// Arduino-ESP32 core 3.1+ (IDF 5.x) changed esp_now_send_cb_t's first argument
// from a raw MAC pointer to a wifi_tx_info_t*; the pre-3.1 uint8_t* signature
// no longer matches esp_now_register_send_cb()'s declared type and fails to
// compile. This board's core is 3.3.x (see esp32_motion_board_log.md), so the
// newer signature is required.
void onSent(const wifi_tx_info_t *txInfo, esp_now_send_status_t status) {
  (void)txInfo;
  if (status == ESP_NOW_SEND_SUCCESS) {
    s_lastOkMs = millis();
  } else {
    s_fail++;
  }
}

}  // namespace

namespace Link {

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(Protocol::CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[LINK] esp_now_init FAILED");
    return;
  }
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, Protocol::ROBOT_MAC, 6);
  peer.channel = Protocol::CHANNEL;
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[LINK] esp_now_add_peer FAILED");
    return;
  }

  Serial.printf("[LINK] ESP-NOW up, channel %u, peer %02X:%02X:%02X:%02X:%02X:%02X\n",
                Protocol::CHANNEL,
                Protocol::ROBOT_MAC[0], Protocol::ROBOT_MAC[1], Protocol::ROBOT_MAC[2],
                Protocol::ROBOT_MAC[3], Protocol::ROBOT_MAC[4], Protocol::ROBOT_MAC[5]);
}

void update(uint32_t now) {
  // Latch every pass — see link.h for why this can't just read
  // Control::captureEvent()/zeroEvent() directly inside the throttled block
  // below.
  if (Control::captureEvent()) s_pendingCapture = true;
  if (Control::zeroEvent())    s_pendingZero    = true;

  if ((now - s_lastSendMs) < Timing::LINK_SEND_MS) return;
  s_lastSendMs = now;

  Protocol::ControlPacket pkt{};
  pkt.magic  = Protocol::MAGIC;
  pkt.seq    = s_seq++;
  pkt.axisX  = Control::txX();
  pkt.axisY  = Control::txY();
  pkt.buttons =
      (uint8_t)((Control::precision() ? 0 : (1 << Protocol::ButtonBit::SPEED_FULL)) |
                (s_pendingCapture      ? (1 << Protocol::ButtonBit::CAPTURE)    : 0) |
                (s_pendingZero         ? (1 << Protocol::ButtonBit::ZERO)       : 0));
  pkt.armed  = Control::armed() ? 1 : 0;
  Protocol::sign(pkt);

  esp_now_send(Protocol::ROBOT_MAC, reinterpret_cast<const uint8_t *>(&pkt), sizeof(pkt));
  s_sent++;

  // Only clear once actually sent, so an event that arrives after the latch
  // check above but before this line isn't lost — it's already folded into
  // pkt.buttons this pass via s_pendingCapture/s_pendingZero, or it waits
  // for the next 20 ms window.
  s_pendingCapture = false;
  s_pendingZero    = false;
}

bool up() {
  if (s_lastOkMs == 0) return false;   // nothing has ever succeeded yet
  return (millis() - s_lastOkMs) < Timing::LINK_TIMEOUT_MS;
}

uint32_t sentCount() { return s_sent; }
uint32_t failCount() { return s_fail; }

}  // namespace Link
