#pragma once
#include "door_link.h"
#include "pir_link.h"
#include "system_controller.h"
#include "access_store.h"
#include "rfid_authority.h"
struct PeerDiagnostics {
  bool enabled=false;
  const char* message="Not configured";
  uint32_t accepted=0,rejected=0,duplicates=0,sent=0,sendErrors=0;
};
struct RadioStatus {
  bool ready=false;
  const char* message="Not configured";
  DoorLink door;
  PirLink pir;
  PeerDiagnostics peers[3];
  uint32_t session=0,sequence=0,accepted=0,rejected=0,duplicates=0,dropped=0;
  uint32_t sent=0,sendErrors=0;
};
using RadioEvent=void (*)(const char*);
bool radioBegin();
void radioTick(uint32_t now,RadioEvent event,SystemController& controller);
const RadioStatus& radioStatus();

const RfidAuthority<AccessStore>& radioCards();
bool radioStartEnrollment(uint32_t now);
void radioCancelEnrollment();
bool radioRemoveCard(const Domotica::CardUid& uid);

uint8_t radioOnlineMask(uint32_t now);
