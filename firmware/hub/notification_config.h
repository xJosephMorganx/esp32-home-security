#pragma once
#if __has_include("email_secrets.h")
#include "email_secrets.h"
#endif
#if __has_include("messenger_secrets.h")
#include "messenger_secrets.h"
#endif
#ifndef HUB_EMAIL_URL
#define HUB_EMAIL_URL ""
#endif
#ifndef HUB_EMAIL_SECRET
#define HUB_EMAIL_SECRET ""
#endif
#ifndef HUB_MESSENGER_APIKEY_1
#define HUB_MESSENGER_APIKEY_1 ""
#endif
#ifndef HUB_MESSENGER_APIKEY_2
#define HUB_MESSENGER_APIKEY_2 ""
#endif
#if __has_include("whatsapp_secrets.h")
#include "whatsapp_secrets.h"
#endif
#ifndef HUB_WHATSAPP_PHONE_1
#define HUB_WHATSAPP_PHONE_1 ""
#endif
#ifndef HUB_WHATSAPP_APIKEY_1
#define HUB_WHATSAPP_APIKEY_1 ""
#endif
#ifndef HUB_WHATSAPP_PHONE_2
#define HUB_WHATSAPP_PHONE_2 ""
#endif
#ifndef HUB_WHATSAPP_APIKEY_2
#define HUB_WHATSAPP_APIKEY_2 ""
#endif
namespace NotificationConfig {
constexpr uint32_t intervalMs=3000; // Local pacing choice, not a provider guarantee.
constexpr uint32_t expiryMs=10UL*60*1000;
constexpr uint32_t rateLimitRetryMs=60000;
constexpr uint8_t maxAttempts=3;
constexpr size_t queueDepth=16;
}
