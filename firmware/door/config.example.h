#pragma once
#include <stdint.h>

// Copy to config.local.h (git-ignored). Never commit real MAC/keys.
namespace DoorConfig {
constexpr bool kProvisioned = false; // Enable only with this pair's real values.
constexpr uint8_t kHubStaMac[6] = {}; // Required: unicast STA MAC, not SoftAP MAC.
constexpr uint8_t kPmk[16] = {};      // Required: explicit 16-byte PMK.
constexpr uint8_t kLmk[16] = {};      // Required: same pair LMK on hub and door.

// Until physically checked, reports stay UNKNOWN; Serial still shows raw levels.
constexpr bool kPolarityVerified = false;
constexpr bool kClosedLevelHigh = false; // Candidate LOW=CLOSED; NOT validated.
constexpr uint32_t kDebounceMs = 40;

// Conservative regulatory domain. Change only to the actual legal local domain.
constexpr char kCountryCode[3] = "01";
constexpr uint8_t kCountryFirstChannel = 1;
constexpr uint8_t kCountryChannelCount = 11;
constexpr uint8_t kChannels[] = {1,2,3,4,5,6,7,8,9,10,11};
constexpr uint8_t kFallbackChannel = 1; // Must appear in kChannels.
}
