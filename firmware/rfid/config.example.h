#pragma once
#include <stdint.h>
namespace RfidConfig {
constexpr bool kProvisioned=false;
constexpr uint8_t kHubStaMac[6]={0x20,0x50,0x0D,0xE6,0xED,0xF4};
constexpr uint8_t kRfidStaMac[6]={}; // Read from the physically identified board.
constexpr uint8_t kPmk[16]={}; // Explicit local provisioning; never print.
constexpr uint8_t kLmk[16]={}; // Unique RFID/hub pair key, never door/PIR's key.
enum class Level : uint8_t { Unspecified, Low, High };
constexpr Level kRedActiveLevel=Level::High;
constexpr Level kGreenActiveLevel=Level::High;
constexpr char kCountryCode[3]="01";
constexpr uint8_t kCountryFirstChannel=1;
constexpr uint8_t kCountryChannelCount=11;
constexpr uint8_t kFallbackChannel=1;
constexpr uint8_t kChannels[]={1,2,3,4,5,6,7,8,9,10,11};
constexpr uint32_t kSampleMs=50;
}

