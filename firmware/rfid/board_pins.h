#pragma once
#include <stdint.h>
// Explicit user correction on 2026-10-20 supersedes the stale context table.
// Matches the functional sketch supplied in this RFID conversation.
namespace RfidPins {
constexpr uint8_t ss=17,rst=23,sck=18,miso=21,mosi=19,irq=22,green=25,red=26;
}
