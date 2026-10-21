#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Domotica {
// Application protocol version, independent of Espressif's ESP-NOW version.
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 16;
constexpr size_t kMaxFrameSize = 36;
constexpr uint32_t kHeartbeatMs = 5000;
constexpr uint32_t kOfflineMs = 15000;
constexpr uint32_t kAckTimeoutMs = 250;
constexpr uint8_t kMaxAttempts = 3; // Includes the original transmission.
constexpr uint32_t kChannelDwellMs = 1000;
constexpr uint32_t kDebounceMs = 40;
constexpr uint32_t kWifiInitialBudgetMs = 10000;
constexpr uint32_t kWifiRetryBudgetMs = 3000;
constexpr uint32_t kWifiRetryPeriodMs = 30000;
constexpr uint8_t kFallbackChannel = 1;
constexpr uint8_t kDefaultSearchChannels[] = {1,2,3,4,5,6,7,8,9,10,11};
constexpr size_t kRxQueueDepth = 8;
constexpr size_t kEventQueueDepth = 8;

enum class Node : uint8_t { Hub = 1, Door = 2, Pir = 3, Rfid = 4 };
enum class Type : uint8_t {
  Hello = 1, Status = 2, Event = 3, StateSync = 4,
  PirHello = 5, PirStatus = 6, PirEvent = 7,
  RfidHello = 8, RfidStatus = 9, CardRequest = 10, CardResult = 11, CardContext = 12
};
enum class DoorState : uint8_t { Unknown = 0, Closed = 1, Open = 2 };
enum class PirState : uint8_t { Unknown = 0, NotReady = 1, NoMotion = 2, Motion = 3 };
enum class SystemState : uint8_t { Unknown = 0, Disarmed = 1, ExitDelay = 2, Armed = 3, Alarm = 4 };
enum class Error : uint8_t { Ok, Buffer, Magic, Version, Type, Length, Identity, Counter, Payload };
enum class CardMode : uint8_t { Disabled = 0, Normal = 1, Enroll = 2 };
enum class CardOutcome : uint8_t {
  InProgress = 0, Unauthorized = 1, Armed = 2, Disarmed = 3, SensorsOffline = 4,
  Added = 5, AlreadyExists = 6, Full = 7, StorageError = 8,
  Expired = 9, WrongContext = 10, Unavailable = 11
};
struct CardUid { uint8_t length = 0; uint8_t bytes[10]{}; };
constexpr bool validUid(const CardUid& uid) {
  if(uid.length!=4 && uid.length!=7 && uid.length!=10)return false;
  for(size_t i=uid.length;i<10;++i)if(uid.bytes[i]!=0)return false;
  return true;
}
constexpr bool sameUid(const CardUid& a,const CardUid& b) {
  if(a.length!=b.length)return false;
  for(size_t i=0;i<10;++i)if(a.bytes[i]!=b.bytes[i])return false;
  return true;
}
constexpr bool outcomeForMode(CardOutcome outcome,CardMode mode) {
  if(mode!=CardMode::Normal && mode!=CardMode::Enroll)return false;
  const auto n=static_cast<uint8_t>(outcome);
  if(n>11)return false;
  if(n>=1 && n<=4)return mode==CardMode::Normal;
  if(n>=5 && n<=7)return mode==CardMode::Enroll;
  return true; // Receipt/pending and failures do not imply authorization or execution.
}

// In-memory objects only. NEVER send these structs using sizeof or reinterpret_cast.
struct Stamp { uint32_t boot = 0; uint32_t sequence = 0; };
struct Frame {
  Type type = Type::Hello;
  Node source = Node::Door;
  Node destination = Node::Hub;
  Stamp stamp{};
  DoorState door = DoorState::Unknown;
  Stamp acknowledged{}; // StateSync: report from the destination node processed by hub.
  uint32_t revision = 0;
  SystemState system = SystemState::Unknown;
  uint8_t channel = 0; // StateSync: actual hub radio channel, not a command to switch.
  PirState pir = PirState::Unknown; // Only PirHello/PirStatus/PirEvent serialize this field.
  uint32_t targetHubBoot = 0; // CardRequest binds to the hub that issued the grant.
  uint32_t contextId = 0;
  CardMode cardMode = CardMode::Disabled;
  CardUid uid{};
  CardOutcome outcome = CardOutcome::InProgress;
};

// Keep the original helper door-only: existing DoorLink uses it for admission.
constexpr bool isReport(Type t) {
  return t == Type::Hello || t == Type::Status || t == Type::Event;
}
constexpr bool isPirReport(Type t) {
  return t == Type::PirHello || t == Type::PirStatus || t == Type::PirEvent;
}
constexpr bool isSensorReport(Type t) { return isReport(t) || isPirReport(t); }
constexpr bool isSensorNode(Node n) { return n == Node::Door || n == Node::Pir; }
constexpr bool isLeafNode(Node n) { return isSensorNode(n) || n==Node::Rfid; }
constexpr bool isRfidHeartbeat(Type t) { return t==Type::RfidHello || t==Type::RfidStatus; }
constexpr bool isKnownType(Type t) {
  return isSensorReport(t) || t==Type::StateSync || isRfidHeartbeat(t)
      || t==Type::CardRequest || t==Type::CardResult || t==Type::CardContext;
}
constexpr bool pirReady(PirState s) { return s == PirState::NoMotion || s == PirState::Motion; }
constexpr size_t payloadSize(Type t) {
  return isSensorReport(t)?1:t==Type::StateSync?14:
      t==Type::CardRequest || t==Type::CardResult?20:t==Type::CardContext?13:0;
}
constexpr bool valid(Stamp s) { return s.boot != 0 && s.sequence != 0; }
constexpr bool same(Stamp a, Stamp b) { return a.boot == b.boot && a.sequence == b.sequence; }
// Zero stamps mean no pending/confirmed report. Check before updating hub tracker.
constexpr bool correlates(const Frame& f, Stamp pending, Stamp confirmed, Node recipient) {
  return f.type == Type::StateSync && f.source == Node::Hub
      && isLeafNode(recipient) && f.destination == recipient && valid(f.acknowledged)
      && ((valid(pending) && same(f.acknowledged, pending))
          || (valid(confirmed) && same(f.acknowledged, confirmed)));
}
// Source-compatible door API; never let a PIR ACK satisfy an existing door caller.
constexpr bool correlates(const Frame& f, Stamp pending, Stamp confirmed) {
  return correlates(f, pending, confirmed, Node::Door);
}
constexpr uint32_t nextCounter(uint32_t n) { return n == UINT32_MAX ? 1 : n + 1; }
// Serial-number arithmetic. Exactly half-range is ambiguous and rejected.
constexpr bool newer(uint32_t a, uint32_t b) { return a != b && uint32_t(a - b) < 0x80000000UL; }
constexpr bool elapsed(uint32_t now, uint32_t then, uint32_t duration) { return uint32_t(now - then) >= duration; }

constexpr Error validate(const Frame& f) {
  if (!isKnownType(f.type)) return Error::Type;
  if (isReport(f.type)) {
    if (f.source != Node::Door || f.destination != Node::Hub) return Error::Identity;
    if (static_cast<uint8_t>(f.door) > 2) return Error::Payload;
  } else if (isPirReport(f.type)) {
    if (f.source != Node::Pir || f.destination != Node::Hub) return Error::Identity;
    if (static_cast<uint8_t>(f.pir) > 3) return Error::Payload;
  } else if(isRfidHeartbeat(f.type) || f.type==Type::CardRequest) {
    if(f.source!=Node::Rfid || f.destination!=Node::Hub)return Error::Identity;
    if(f.type==Type::CardRequest &&
       (!f.targetHubBoot || !f.contextId || !validUid(f.uid)
        || (f.cardMode!=CardMode::Normal && f.cardMode!=CardMode::Enroll)))return Error::Payload;
  } else {
    if (f.source != Node::Hub || !isLeafNode(f.destination)) return Error::Identity;
    if (!valid(f.acknowledged)) return Error::Counter;
    if(f.type==Type::CardContext || f.type==Type::CardResult) {
      if(f.destination!=Node::Rfid)return Error::Identity;
      if(!f.contextId || static_cast<uint8_t>(f.cardMode)>2)return Error::Payload;
      if(f.type==Type::CardContext) {
        if(f.contextId!=f.stamp.sequence)return Error::Payload;
        return valid(f.stamp)?Error::Ok:Error::Counter;
      }
      if(!outcomeForMode(f.outcome,f.cardMode))return Error::Payload;
    }
    if (static_cast<uint8_t>(f.system) > 4 || f.channel < 1 || f.channel > 14) return Error::Payload;
    if ((f.system == SystemState::Unknown) != (f.revision == 0)) return Error::Payload;
  }
  return valid(f.stamp) ? Error::Ok : Error::Counter;
}
constexpr void put32(uint8_t* out, uint32_t n) {
  for (size_t i = 0; i < 4; ++i) out[i] = uint8_t(n >> (8 * i));
}
constexpr uint32_t get32(const uint8_t* in) {
  return uint32_t(in[0]) | (uint32_t(in[1]) << 8) | (uint32_t(in[2]) << 16) | (uint32_t(in[3]) << 24);
}
// Failure leaves the output buffer untouched and sets written to zero.
constexpr Error encode(const Frame& f, uint8_t* out, size_t capacity, size_t& written) {
  written = 0;
  const Error result = validate(f);
  if (result != Error::Ok) return result;
  const size_t payload = payloadSize(f.type);
  if (!out || capacity < kHeaderSize + payload) return Error::Buffer;
  out[0] = 'D'; out[1] = 'M'; out[2] = kVersion; out[3] = static_cast<uint8_t>(f.type);
  out[4] = static_cast<uint8_t>(f.source); out[5] = static_cast<uint8_t>(f.destination);
  put32(out + 6, f.stamp.boot); put32(out + 10, f.stamp.sequence);
  out[14] = uint8_t(payload); out[15] = 0;
  if (isReport(f.type)) out[16] = static_cast<uint8_t>(f.door);
  else if (isPirReport(f.type)) out[16] = static_cast<uint8_t>(f.pir);
  else if(f.type==Type::CardRequest) {
    put32(out+16,f.targetHubBoot);put32(out+20,f.contextId);
    out[24]=static_cast<uint8_t>(f.cardMode);out[25]=f.uid.length;
    for(size_t i=0;i<10;++i)out[26+i]=f.uid.bytes[i];
  } else if(f.type==Type::CardContext) {
    put32(out+16,f.acknowledged.boot);put32(out+20,f.acknowledged.sequence);
    put32(out+24,f.contextId);out[28]=static_cast<uint8_t>(f.cardMode);
  } else if(f.type==Type::CardResult) {
    put32(out+16,f.acknowledged.boot);put32(out+20,f.acknowledged.sequence);
    put32(out+24,f.contextId);out[28]=static_cast<uint8_t>(f.cardMode);
    out[29]=static_cast<uint8_t>(f.outcome);put32(out+30,f.revision);
    out[34]=static_cast<uint8_t>(f.system);out[35]=f.channel;
  } else if(f.type==Type::StateSync) {
    put32(out + 16, f.acknowledged.boot); put32(out + 20, f.acknowledged.sequence);
    put32(out + 24, f.revision); out[28] = static_cast<uint8_t>(f.system); out[29] = f.channel;
  }
  written = kHeaderSize + payload;
  return Error::Ok;
}
// Decode is structural validation only, not MAC authentication or replay protection.
// A failed decode NEVER changes out. Run the peer/session checks before applying it.
constexpr Error decode(const uint8_t* in, size_t length, Frame& out) {
  if (!in) return Error::Buffer;
  if (length < kHeaderSize || length > kMaxFrameSize) return Error::Length;
  if (in[0] != 'D' || in[1] != 'M') return Error::Magic;
  if (in[2] != kVersion) return Error::Version;
  Frame f{};
  f.type = static_cast<Type>(in[3]);
  const size_t payload = payloadSize(f.type);
  if (!isKnownType(f.type)) return Error::Type;
  if (in[14] != payload || in[15] != 0 || length != kHeaderSize + payload) return Error::Length;
  f.source = static_cast<Node>(in[4]); f.destination = static_cast<Node>(in[5]);
  f.stamp = {get32(in + 6), get32(in + 10)};
  if (isReport(f.type)) f.door = static_cast<DoorState>(in[16]);
  else if (isPirReport(f.type)) f.pir = static_cast<PirState>(in[16]);
  else if(f.type==Type::CardRequest) {
    f.targetHubBoot=get32(in+16);f.contextId=get32(in+20);
    f.cardMode=static_cast<CardMode>(in[24]);f.uid.length=in[25];
    for(size_t i=0;i<10;++i)f.uid.bytes[i]=in[26+i];
  } else if(f.type==Type::CardContext) {
    f.acknowledged={get32(in+16),get32(in+20)};
    f.contextId=get32(in+24);f.cardMode=static_cast<CardMode>(in[28]);
  } else if(f.type==Type::CardResult) {
    f.acknowledged={get32(in+16),get32(in+20)};
    f.contextId=get32(in+24);f.cardMode=static_cast<CardMode>(in[28]);
    f.outcome=static_cast<CardOutcome>(in[29]);f.revision=get32(in+30);
    f.system=static_cast<SystemState>(in[34]);f.channel=in[35];
  } else if(f.type==Type::StateSync) {
    f.acknowledged = {get32(in + 16), get32(in + 20)};
    f.revision = get32(in + 24); f.system = static_cast<SystemState>(in[28]); f.channel = in[29];
  }
  const Error result = validate(f);
  if (result == Error::Ok) out = f;
  return result;
}

enum class Order : uint8_t { First, NewSession, Fresh, Duplicate, Stale, Invalid };
struct PeerTracker {
  bool seen = false;
  Stamp last{};
  uint32_t receivedAt = 0;
  constexpr Order classify(Stamp s) const {
    if (!valid(s)) return Order::Invalid;
    if (!seen) return Order::First;
    if (s.boot != last.boot) return newer(s.boot, last.boot) ? Order::NewSession : Order::Stale;
    if (s.sequence == last.sequence) return Order::Duplicate;
    return newer(s.sequence, last.sequence) ? Order::Fresh : Order::Stale;
  }
  // Call only after authentication, decode, destination and ACK-correlation checks.
  constexpr Order accept(Stamp s, uint32_t now) {
    const Order order = classify(s);
    if (order == Order::First || order == Order::NewSession || order == Order::Fresh) {
      seen = true; last = s; receivedAt = now;
    }
    return order;
  }
  constexpr bool online(uint32_t now) const { return seen && !elapsed(now, receivedAt, kOfflineMs); }
};
constexpr bool ledOn(SystemState state, bool hubOnline) {
  return hubOnline && (state == SystemState::Armed || state == SystemState::Alarm);
}
} // namespace Domotica
