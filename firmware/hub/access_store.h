#pragma once
#include <nvs.h>
#include <string.h>
#include "access_list.h"

// One versioned NVS blob is replaced atomically; authorize only a verified read.
class AccessStore {
public:
  AccessList list;
  bool ready=false;
  uint32_t generation=0;
  bool begin() {
    if(nvs_open("hub-access",NVS_READWRITE,&handle_)!=ESP_OK)return false;
    opened_=true;
    size_t size=0;
    auto error=nvs_get_blob(handle_,"cards",nullptr,&size);
    if(error==ESP_ERR_NVS_NOT_FOUND)return save(AccessList{});
    return reload();
  }
  bool save(const AccessList& candidate) {
    uint8_t bytes[kAccessBlobSize];
    if(!opened_||!encodeAccess(candidate,bytes))return false;
    ready=false;
    bool written=nvs_set_blob(handle_,"cards",bytes,sizeof(bytes))==ESP_OK
        && nvs_commit(handle_)==ESP_OK;
    // Even a failed commit can have persisted: always reconcile from storage.
    bool loaded=reload();
    uint8_t verified[kAccessBlobSize];
    return written && loaded && encodeAccess(list,verified) && !memcmp(bytes,verified,sizeof(bytes));
  }
private:
  nvs_handle_t handle_=0;
  bool opened_=false;
  bool reload() {
    ready=false;
    uint8_t bytes[kAccessBlobSize];size_t size=sizeof(bytes);AccessList verified;
    if(!opened_||nvs_get_blob(handle_,"cards",bytes,&size)!=ESP_OK
        ||!decodeAccess(bytes,size,verified)){++generation;return false;}
    list=verified;ready=true;++generation;return true;
  }
};
