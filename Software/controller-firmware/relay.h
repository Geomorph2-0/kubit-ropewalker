/*
 * relay.h — DEPRECATED, please delete this file and relay.cpp.
 *
 * This was a UART2-based (GPIO16/17) tee for mirroring Serial output to the
 * S3. It's been superseded: the S3 debug link now taps UART0 (GPIO1 TX0 /
 * GPIO3 RX0) directly, which already carries every Serial.print byte-for-byte
 * with no firmware involvement at all — see the "TEMPORARY BRING-UP TAP"
 * note in controller-firmware.ino.
 *
 * Nothing includes this header anymore. It's left in place only because this
 * assistant has no way to delete files on your machine — safe to remove
 * both relay.h and relay.cpp by hand.
 */
