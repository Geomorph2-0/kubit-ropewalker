#pragma once
#include <Arduino.h>

/*
 * control.h — the policy layer. Everything that decides *what the controller
 * means* lives here, and nowhere else.
 *
 * Joystick and Buttons report facts. This module turns those facts into an
 * intent: armed or not, how much of the stick's travel to use, and what actually
 * gets transmitted. Stage 4's packet is built from these accessors, and stage 6's
 * failsafe rules will live here too — so the whole safety story stays readable
 * on one screen.
 */
namespace Control {

void begin();

// Consumes this pass's Joystick and Buttons state. Call after both have updated.
void update(uint32_t now);

bool armed();
bool precision();

/* What would actually be sent, in -1000 .. +1000. Already scaled by the speed
 * mode and forced to zero while disarmed — the robot will ignore a live value
 * anyway, but sending real zeros means no single bug on either side can drive
 * the motors on its own.
 */
int16_t txX();
int16_t txY();

uint32_t captureCount();
uint32_t zeroCount();

/* One-shot events, valid only for the pass that set them. Stage 4 turns these
 * into packet flags; for now they exist so nothing has to re-derive them from
 * button edges.
 */
bool captureEvent();
bool zeroEvent();

}  // namespace Control
