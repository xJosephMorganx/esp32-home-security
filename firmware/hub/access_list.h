#pragma once
#include <RfidContract.h>

struct AccessList {
  uint8_t count=0;
  Domotica::CardUid cards[Domotica::kMaxAccessCards]{};
  constexpr int find(const Domotica::CardUid& uid) const {
    for(uint8_t i=0;i<count;++i)if(Domotica::sameUid(cards[i],uid))return i;
    return -1;
  }
  constexpr bool valid() const {
    if(count>Domotica::kMaxAccessCards)return false;
    for(uint8_t i=0;i<count;++i) {
      if(!Domotica::validUid(cards[i]))return false;
      for(uint8_t j=0;j<i;++j)if(Domotica::sameUid(cards[i],cards[j]))return false;
    }
    return true;
  }
  constexpr bool add(const Domotica::CardUid& uid) {
    if(!Domotica::validUid(uid)||find(uid)>=0||count>=Domotica::kMaxAccessCards)return false;
    cards[count++]=uid;return true;
  }
  constexpr bool remove(const Domotica::CardUid& uid) {
    int i=find(uid);if(i<0)return false;
    for(int j=i;j<int(count)-1;++j)cards[j]=cards[j+1];
    cards[--count]={};return true;
  }
};
constexpr size_t kAccessBlobSize=5+Domotica::kMaxAccessCards*11+4;
constexpr uint32_t accessCrc(const uint8_t* bytes,size_t size) {
  uint32_t crc=0xffffffff;
  for(size_t i=0;i<size;++i) {
    crc^=bytes[i];
    for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320UL:0);
  }
  return ~crc;
}
constexpr bool encodeAccess(const AccessList& list,uint8_t* bytes) {
  if(!list.valid())return false;
  for(size_t i=0;i<kAccessBlobSize;++i)bytes[i]=0;
  bytes[0]='A';bytes[1]='C';bytes[2]=1;bytes[4]=list.count;
  for(uint8_t i=0;i<list.count;++i) {
    bytes[5+i*11]=list.cards[i].length;
    for(int j=0;j<10;++j)bytes[6+i*11+j]=list.cards[i].bytes[j];
  }
  Domotica::put32(bytes+kAccessBlobSize-4,accessCrc(bytes,kAccessBlobSize-4));return true;
}
constexpr bool decodeAccess(const uint8_t* bytes,size_t size,AccessList& out) {
  if(size!=kAccessBlobSize||bytes[0]!='A'||bytes[1]!='C'||bytes[2]!=1||bytes[3]!=0
      ||bytes[4]>Domotica::kMaxAccessCards
      ||Domotica::get32(bytes+size-4)!=accessCrc(bytes,size-4))return false;
  AccessList candidate;candidate.count=bytes[4];
  for(uint8_t i=0;i<candidate.count;++i) {
    candidate.cards[i].length=bytes[5+i*11];
    for(int j=0;j<10;++j)candidate.cards[i].bytes[j]=bytes[6+i*11+j];
  }
  for(size_t i=5+candidate.count*11;i<size-4;++i)if(bytes[i])return false;
  if(!candidate.valid())return false;
  out=candidate;return true;
}
