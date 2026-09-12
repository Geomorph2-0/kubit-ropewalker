/*
 * robot-stage4-espnow-receiver.ino — stage 4 exit-test sketch for the ROBOT.
 *
 * Minimal ESP-NOW receiver: prints every ControlPacket it gets from the
 * controller (F4:2D:C9:71:7B:0C). This is a bring-up sketch, not robot
 * firmware — it drives nothing. It only proves the link, per
 * controller_plan.md's stage 4 exit test: "Robot prints packets; green
 * tracks robot power" (green is the CONTROLLER's LED; this sketch's job is
 * just to make sure something is actually arriving for it to track).
 *
 * TARGET BOARD: classic ESP32-D0WD-V3, 38-pin WROOM-32 DevKit.
 *               Robot MAC F4:2D:C9:71:0A:7C.
 *
 * No peer registration is needed on this side — esp_now_add_peer() is only
 * required to SEND. Any ESP-NOW packet addressed to this board's MAC on the
 * matching channel reaches the receive callback regardless of peer list.
 *
 * Uses Software/shared/protocol.h — see that file's header for the
 * symlink-vs-copy caveat; this folder currently holds a literal copy, not a
 * symlink (this assistant has no way to create a real symlink on your
 * machine through the file bridge it's using).
 *
 * Real robot firmware — motor driver, encoders, the actual GPIO pin map —
 * is still an open item (see esp32_motion_board_log.md "Next, when picked
 * up"). This sketch deliberately touches none of that, so it can't collide
 * with whatever that pin map turns out to be.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

#include "protocol.h"

namespace {

uint32_t s_received = 0;
uint32_t s_rejected = 0;   // bad length, bad magic, or bad CRC
uint32_t s_lost     = 0;   // gaps inferred from seq
bool     s_haveSeq  = false;
uint16_t s_lastSeq  = 0;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;

  if (len != (int)sizeof(Protocol::ControlPacket)) {
    s_rejected++;
    return;
  }

  Protocol::ControlPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));

  if (!Protocol::verify(pkt)) {
    s_rejected++;
    return;
  }

  if (s_haveSeq) {
    const uint16_t expected = (uint16_t)(s_lastSeq + 1);
    if (pkt.seq != expected) {
      // Wrapping-safe: correct whether or not seq has wrapped past 65535.
      s_lost += (uint16_t)(pkt.seq - expected);
    }
  }
  s_lastSeq = pkt.seq;
  s_haveSeq = true;
  s_received++;

  Serial.printf("#%-5u  X %+5d  Y %+5d  SPEED:%-9s  CAP:%d  ZERO:%d  ARMED:%d  "
                "(rx=%lu rej=%lu lost=%lu)\n",
                pkt.seq, pkt.axisX, pkt.axisY,
                (pkt.buttons & (1 << Protocol::ButtonBit::SPEED_FULL)) ? "FULL" : "PRECISION",
                (pkt.buttons & (1 << Protocol::ButtonBit::CAPTURE)) ? 1 : 0,
                (pkt.buttons & (1 << Protocol::ButtonBit::ZERO))    ? 1 : 0,
                pkt.armed,
                (unsigned long)s_received, (unsigned long)s_rejected, (unsigned long)s_lost);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);

  Serial.println();
  Serial.println("=== Robot — stage 4 ESP-NOW receiver (bring-up only) ===");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(Protocol::CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("esp_now_init FAILED — halting");
    while (true) { delay(1000); }
  }
  esp_now_register_recv_cb(onReceive);

  Serial.printf("Listening on channel %u for controller %02X:%02X:%02X:%02X:%02X:%02X\n",
                Protocol::CHANNEL,
                Protocol::CONTROLLER_MAC[0], Protocol::CONTROLLER_MAC[1], Protocol::CONTROLLER_MAC[2],
                Protocol::CONTROLLER_MAC[3], Protocol::CONTROLLER_MAC[4], Protocol::CONTROLLER_MAC[5]);
  Serial.println();
}

void loop() {
  // Everything happens in onReceive(); nothing to poll.
  delay(1000);
}
