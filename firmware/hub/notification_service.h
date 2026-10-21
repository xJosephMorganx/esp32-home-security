#pragma once
#include "notification_policy.h"
struct NotificationStatus {
  uint8_t recipients=0;
  uint32_t pending=0,accepted=0,failed=0,unknown=0,dropped=0,expired=0;
  int http[2]={0,0};
  const char* message="Not configured";
};
void notificationBegin();
void notificationWorkerReady(bool ready);
void notificationPump(); // Only the existing network worker calls this; serial TLS.
void notificationObserve(bool armed,uint8_t onlineMask); // Main loop only.
void notificationEvent(Notice kind); // Main loop only, never a radio callback.
NotificationStatus notificationStatus(uint8_t channel=0);
void notificationTest(uint8_t channel);
void notificationSetVisualCallback(void (*callback)(Notice));
const char* notificationChannelName(uint8_t channel);
