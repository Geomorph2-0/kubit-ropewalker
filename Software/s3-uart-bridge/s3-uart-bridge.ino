/*
 * s3-uart-bridge.ino — TEMPORARY bring-up relay.
 *
 * Forwards whatever the controller (classic ESP32, Software/controller-
 * firmware/) sends on its serial port straight through to this board's own
 * USB-UART bridge, so the controller's banner, status line and event log
 * can all be read from the S3's Serial Monitor with only one USB cable
 * plugged in.
 *
 * This is throwaway scaffolding, NOT the permanent robot<->S3 link recorded
 * as "decided: UART, pins/framing not yet specified" in
 * claude/controller_plan.md §6 and claude/esp32_devkit_38pin_pinout.md — this
 * sketch talks to the ground CONTROLLER, over a wire it will not have once
 * it is untethered on its own battery (see controller_plan.md §1: it is
 * meant to run off an 18650, not a cable to the robot's camera board).
 *
 * It also does nothing else: no camera, no WiFi, no SD. That matters here —
 * a byte-for-byte relay only stays clean because nothing else on this board
 * is writing to Serial at the same time. Once this logic is folded into the
 * real camera/upload firmware, tag and line-buffer instead of relaying raw
 * bytes, or the controller's lines will interleave with the camera's own
 * log output.
 *
 * Delete this sketch once a second USB cable (bench) or the real ESP-NOW +
 * robot-UART path (field) replaces it.
 *
 * WIRING (temporary)
 *   Classic TX0 (GPIO1)  ->  S3 RXD1 pin below
 *   Classic RX0 (GPIO3)  <-  S3 TXD1 pin below
 *   Classic GND           -- S3 GND    <-- do not skip this; see the
 *                                          grounding note in
 *                                          claude/esp32_motion_board_log.md
 *
 *   *** THE CLASSIC SIDE USES ITS OWN USB-SERIAL PINS (UART0) ***
 *   GPIO1/GPIO3 on the classic are also wired internally to its onboard
 *   CP2102 USB-serial chip. That's deliberate here — it's what lets the
 *   controller's firmware reach this board with zero code changes — but it
 *   means the classic's OWN USB cable must never be plugged in at the same
 *   time as this wire. Whenever it is, the CP2102 and this board both drive
 *   the classic's RX0 line at once: two active outputs fighting over one
 *   node, which can degrade or damage a chip. Pick one: the classic's own
 *   USB, or this wire. Never both.
 *
 *   *** CONFIRM RXD1 / TXD1 ON THIS BOARD BEFORE WIRING ***
 *   This project's own esp32s3_spec_sheet.md §4 says the Freenove camera pin
 *   map for this board revision is "to be confirmed" — meaning no GPIO on
 *   this board can be assumed free yet. The S3's UART is fully remappable
 *   through its GPIO matrix, so RXD1/TXD1 below are placeholders, not a
 *   verified-safe pair. Check them against the camera/SD/RGB-LED pins on the
 *   physical board, change the two constants if needed, and only then wire
 *   it up.
 */

#include <Arduino.h>

constexpr int      RXD1      = 17;      // <-- CONFIRM: receives classic's TX0/GPIO1
constexpr int      TXD1      = 18;      // <-- CONFIRM: drives classic's RX0/GPIO3
constexpr uint32_t LINK_BAUD = 115200;  // must match SERIAL_BAUD in the classic's config.h

void setup() {
  Serial.begin(115200);   // this board's USB-UART bridge -> Arduino Serial Monitor
  while (!Serial && millis() < 3000) { delay(10); }
  delay(200);

  Serial1.begin(LINK_BAUD, SERIAL_8N1, RXD1, TXD1);

  Serial.println();
  Serial.println("=== S3 UART bridge (TEMPORARY — see file header) ===");
  Serial.printf("Relaying UART1 (RX %d / TX %d) at %lu baud -> this USB port\n",
                RXD1, TXD1, (unsigned long)LINK_BAUD);
  Serial.println("Everything below is the CONTROLLER's own serial output.");
  Serial.println("Reminder: the controller's OWN USB cable must be unplugged");
  Serial.println("whenever this wire is connected — see the file header.");
  Serial.println();
}

void loop() {
  // Controller -> this board -> USB, byte for byte. No line buffering or
  // tagging needed: nothing else on this board writes to Serial while this
  // sketch runs, and one ~100-byte status line every 100 ms is far below
  // what the USB-UART bridge can carry at 115200 baud.
  while (Serial1.available()) {
    Serial.write(Serial1.read());
  }

  // Reverse path: type in this Serial Monitor to send the controller's
  // 'c' / 't' / 'v<volts>' commands back over the wire.
  while (Serial.available()) {
    Serial1.write(Serial.read());
  }
}
