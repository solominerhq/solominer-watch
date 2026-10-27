#pragma once
#include "battery_state.h"

namespace battery {
// Read the AXP2101 registers explicitly: XPowersLib's convenience accessors
// collapse I2C failures to 0 mV / false / stopped, losing validity information.
// Register definitions: X-Powers AXP2101 datasheet sections 6.13.2.1/.2/.88.
// Reader returns one byte (0..255) or -1 on failure; caller owns the I2C lock.
template<class Reader> Sample readSample(Reader read) {
    Sample s;
    const int status0 = read(0x00);
    const int status1 = read(0x01);
    const int high = read(0x34);
    const int low = read(0x35);
    const int highAgain = read(0x34);
    s.gauge = read(0xA4);
    const int status0Again = read(0x00);
    const int status1Again = read(0x01);

    if (status0 < 0 || status1 < 0 || status0Again < 0 || status1Again < 0)
        return s;
    // Reject a mixed plug/unplug, battery-removal or charger-state snapshot.
    if (((status0 ^ status0Again) & 0x28) || ((status1 ^ status1Again) & 0x6F))
        return s;
    s.direction = (status1 >> 5) & 3;
    s.charge = status1 & 7;
    if (s.direction == 3 || s.charge > 5) return s; // reserved values
    s.statusValid = true;
    s.present = (status0 & 0x08) != 0;
    s.usb = (status0 & 0x20) != 0;
    s.inputLimited = (status1 & 0x08) != 0;
    if (high >= 0 && low >= 0 && highAgain >= 0 &&
        (high & 0x1F) == (highAgain & 0x1F))
        s.millivolts = ((high & 0x1F) << 8) | low;
    return s;
}
} // namespace battery
