#pragma once
#include <Arduino.h>

/*
 * link.h — stage 4: ESP-NOW control link, controller -> robot.
 *
 * The addition called for by controller_plan.md's stage 4 and the README
 * roadmap: "protocol.h + link.*, no changes to existing modules." It reads
 * Control's already-computed intent (txX/txY/armed/precision/captureEvent/
 * zeroEvent) and nothing else — it does not decide anything, only packages
 * and sends what Control already decided. joystick.*, buttons.*, battery.*
 * and control.* are untouched; only config.h (two new Timing constants) and
 * ui.cpp (green now reflects link state instead of a bare heartbeat) change,
 * because ui.cpp's own header already names itself as the one place LED
 * semantics are allowed to move.
 *
 * Packet format is protocol.h, the canonical copy at Software/shared/. See
 * that file's header for why a shared struct instead of two independent
 * ones, and for the symlink-vs-copy caveat currently in effect.
 *
 * WIRING: none — this is a 2.4 GHz radio link, no pins involved. It does,
 * however, retire every ADC2 GPIO from analog use the instant Link::begin()
 * runs; see the pin-invalidation note added to esp32_devkit_38pin_pinout.md.
 * This board's current design already avoids that (ARM/SPEED/CAPTURE/STICK
 * are read digitally, and both analog inputs are on ADC1), so nothing here
 * breaks — it only forecloses ever reading GPIO26 (or any other ADC2 pin)
 * with analogRead() in the future.
 */
namespace Link {

// WiFi in station mode (no AP join), fixed channel, esp_now_init, registers
// the robot as a unicast peer. Call once in setup(), after Serial.begin() —
// prints what it's doing and whether it succeeded.
void begin();

// Latches CAPTURE/ZERO one-shot events every call (cheap; call every loop
// pass, unthrottled, like every other module's update()), and every
// Timing::LINK_SEND_MS builds and sends one ControlPacket from Control's
// current state.
//
// The latch matters: Control::update() clears captureEvent()/zeroEvent()
// on every loop pass, but this function's send half only samples Control
// once every 20 ms. Without latching, a CAPTURE press that lands on a loop
// iteration between two throttled sends would be true for exactly one pass
// and never actually observed — dropped, silently, with no error anywhere.
// Latching turns "true for one loop iteration" into "true for the next
// packet sent," which is the guarantee the one-shot event actually needs.
void update(uint32_t now);

// True if the most recent send was ESP_NOW_SEND_SUCCESS and it completed
// within Timing::LINK_TIMEOUT_MS. Timeout, not event-driven, for the same
// reason telemetry will be at stage 5: nothing arrives to tell you the link
// died, so silence past the deadline IS the failure. ui.cpp reads this for
// the green LED, per stage 4's spec: "Green from send-callback status."
bool up();

// Packets sent / send-callback failures, for the status line.
uint32_t sentCount();
uint32_t failCount();

}  // namespace Link
