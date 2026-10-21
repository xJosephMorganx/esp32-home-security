#pragma once
// Copy to radio_secrets.h (ignored by Git). Do not use placeholder keys.
// MAC STA of the DOOR, 6 bytes as 12 hexadecimal characters, no separators.
#define HUB_DOOR_MAC_HEX ""
// 16 bytes each as 32 hexadecimal characters; provision with the door firmware.
#define HUB_ESPNOW_PMK_HEX ""
#define HUB_DOOR_LMK_HEX ""
// Optional PIR peer: its own confirmed STA MAC and unique pair LMK.
// Missing/invalid PIR configuration disables only this peer.
#define HUB_PIR_MAC_HEX ""
#define HUB_PIR_LMK_HEX ""

// RFID STA MAC and unique RFID/hub LMK.
#define HUB_RFID_MAC_HEX ""
#define HUB_RFID_LMK_HEX ""
