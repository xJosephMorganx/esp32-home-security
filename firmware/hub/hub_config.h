#pragma once
// No credentials in tracked sources. Copy secrets.example.h to secrets.h locally.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef HUB_WIFI_SSID
#define HUB_WIFI_SSID ""
#endif
#ifndef HUB_WIFI_PASSWORD
#define HUB_WIFI_PASSWORD ""
#endif
namespace Config {
// Rotate the entire landscape image AND touch mapping by 180 degrees if needed.
constexpr bool LANDSCAPE_FLIP=false;
constexpr char VERSION[]="hub-wa-0.6.0";
constexpr char TIMEZONE[]="CST6"; // Zapopan: UTC-6, no seasonal clock change.
constexpr uint32_t WEATHER_PERIOD_MS=15UL*60*1000;
constexpr uint32_t WEATHER_STALE_MS=45UL*60*1000;
// Used only before an AP channel has been saved; must be legal in the radio region.
constexpr uint8_t LOCAL_CHANNEL=1;
}
