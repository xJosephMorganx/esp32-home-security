#pragma once
#include <stdint.h>
bool wifiControlBegin();
void networkTick(uint32_t now);
uint8_t networkChannel();
bool networkRadioStable();
