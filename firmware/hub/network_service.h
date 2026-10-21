#pragma once
#include "ui_model.h"
bool networkBegin();
bool networkReadWeather(Weather& result);
void networkTick(uint32_t now);
uint8_t networkChannel();
bool networkRadioStable();
