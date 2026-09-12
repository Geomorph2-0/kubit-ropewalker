#pragma once
#include <Arduino.h>
#include <string.h>

/*
 * protocol.h — the wire format for ESP-NOW between the two classic ESP32s.
 *
 * CANONICAL COPY: Software/shared/protocol.h. Both Software/controller-firmware/
 * and the robot-side sketch need this exact file, byte for byte — ESP-NOW
 * delivers raw bytes with no schema, so if one end's struct layout drifts
 * from the other's even by one field, packets decode into a plausible-looking
 * WRONG command instead of an error.
 *
 * The intended mechanism, per the README's stage 4 note, is a relative
 * symlink from each sketch folder to this file (git stores symlinks
 * natively; the Arduino IDE follows them on Linux — a #include
 * "../shared/protocol.h" path does not work, because the IDE copies the
 * sketch folder to a build directory at compile time). This assistant has no
 * way to create a symlink on your machine through the file bridge it's
 * using, so what's shipped alongside this file are two literal COPIES: one
 * in controller-firmware/, one in the robot's test sketch folder. They are
 * identical right now. Until you replace them with real symlinks, a change
 * to one will NOT reach the other. From each sketch folder:
 *
 *   rm protocol.h
 *   ln -s ../../shared/protocol.h protocol.h      # from Software/tests/Arduino/<sketch>/
 *   ln -s ../shared/protocol.h protocol.h         # from Software/controller-firmware/
 *
 * Both ends are the same silicon (ESP32-D0WD-V3), so struct packing already
 * agrees, but the struct is packed explicitly anyway: a difference here
 * should come from a deliberate protocol change, not from whatever the
 * compiler's default alignment happens to be.
 */

namespace Protocol {

// Fixed ESP-NOW channel. Arbitrary choice — not previously specified anywhere
// in the project docs — since ESP-NOW is peer-to-peer and never associates
// with an access point, so this does not need to match the venue WiFi
// channel the S3 uses for uploads; it only has to match between these two
// boards. Change it on both ends together if you ever see interference.
constexpr uint8_t CHANNEL = 1;

// WiFi STA MACs, from esp32_motion_board_log.md — measured, not derived.
constexpr uint8_t CONTROLLER_MAC[6] = {0xF4, 0x2D, 0xC9, 0x71, 0x7B, 0x0C};
constexpr uint8_t ROBOT_MAC[6]      = {0xF4, 0x2D, 0xC9, 0x71, 0x0A, 0x7C};

constexpr uint8_t MAGIC = 0xC5;   // arbitrary; just not 0x00 or 0xFF

// Bit positions within ControlPacket::buttons.
namespace ButtonBit {
  constexpr uint8_t SPEED_FULL = 0;   // 1 = FULL speed mode, 0 = PRECISION
  constexpr uint8_t CAPTURE    = 1;   // one-shot, latched: SAR capture requested
  constexpr uint8_t ZERO       = 2;   // one-shot, latched: zero distance requested
}

#pragma pack(push, 1)
struct ControlPacket {
  uint8_t  magic;
  uint16_t seq;
  int16_t  axisX;    // -1000..+1000 — Control::txX(), already scaled + deadzoned
  int16_t  axisY;
  uint8_t  buttons;  // bitmask, see ButtonBit
  uint8_t  armed;    // 0 or 1
  uint8_t  crc;      // crc8() over every byte above
};
#pragma pack(pop)

static_assert(sizeof(ControlPacket) == 10,
              "ControlPacket layout changed size — update both ends together");

// CRC-8, poly 0x07, computed over every field except crc itself. Not
// cryptographic, and not meant to be: ESP-NOW's own link layer already has
// an FCS, so a bit error in flight is already unlikely to arrive at all.
// This second, application-level check exists to catch a version skew
// between the two copies of this file instead — the case a link-layer
// checksum can't see, because both ends agree on the bytes, just not on
// what they mean.
inline uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

// Fills packet.crc from every other field. Call last, right before sending.
inline void sign(ControlPacket &p) {
  p.crc = crc8(reinterpret_cast<const uint8_t *>(&p), sizeof(p) - sizeof(p.crc));
}

// magic must match AND the CRC must recompute clean before anything else in
// the packet is trusted.
inline bool verify(const ControlPacket &p) {
  if (p.magic != MAGIC) return false;
  return crc8(reinterpret_cast<const uint8_t *>(&p), sizeof(p) - sizeof(p.crc)) == p.crc;
}

}  // namespace Protocol
