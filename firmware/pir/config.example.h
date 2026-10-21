#pragma once
#include <stdint.h>

// Copy to config.local.h (ignored by Git). Enable only after the hub has PIR multipeer support.
namespace PirConfig {
constexpr bool kProvisioned = false; // Enable with verified MACs and pair keys only.
constexpr uint8_t kHubStaMac[6] = {0x20,0x50,0x0D,0xE6,0xED,0xF4}; // User-confirmed.
constexpr uint8_t kPirStaMac[6] = {}; // Obtain from the identified PIR board.
constexpr uint8_t kPmk[16] = {}; // Not provisioned.
constexpr uint8_t kLmk[16] = {}; // Pair-specific; not provisioned.

// Unspecified electrical values stay unspecified, never guessed from door wiring.
enum class Level : uint8_t { Unspecified, Low, High };
constexpr Level kMotionLevel = Level::Unspecified;
constexpr Level kLedActiveLevel = Level::Unspecified;
constexpr bool kSensorConfigurationConfirmed = false;
constexpr uint32_t kWarmupMs = 0; // Unset: must be >0 before valid sensor facts.
constexpr uint32_t kFilterMs = 0; // Unset; select explicitly, not door's debounce.
constexpr uint32_t kSampleMs = 10; // Proposed software polling period.
constexpr const char* kSensorPhysicalMode = "UNCONFIRMED"; // Jumper/retrigger mode.

// Conservative domain, matching the shared contract. Confirm local legal channels.
constexpr char kCountryCode[3] = "01";
constexpr uint8_t kCountryFirstChannel = 1;
constexpr uint8_t kCountryChannelCount = 11;
constexpr uint8_t kFallbackChannel = 1;
constexpr uint8_t kChannels[] = {1,2,3,4,5,6,7,8,9,10,11};
}
