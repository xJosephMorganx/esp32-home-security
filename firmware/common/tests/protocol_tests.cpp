#include "../src/DomoticaProtocol.h"
using namespace Domotica;

constexpr Frame report(Type type = Type::Hello) {
  Frame f{}; f.type = type; f.stamp = {0x12345678UL, 0x01020304UL}; f.door = DoorState::Open;
  return f;
}
constexpr Frame sync() {
  Frame f{}; f.type = Type::StateSync; f.source = Node::Hub; f.destination = Node::Door;
  f.stamp = {9, 10}; f.acknowledged = {7, 8}; f.revision = 11;
  f.system = SystemState::Armed; f.channel = 6; return f;
}
constexpr bool golden() {
  const uint8_t expected[] = {0x44,0x4d,1,1,2,1,0x78,0x56,0x34,0x12,4,3,2,1,1,0,2};
  uint8_t bytes[kMaxFrameSize] = {}; size_t n = 0;
  if (encode(report(), bytes, sizeof(bytes), n) != Error::Ok || n != sizeof(expected)) return false;
  for (size_t i = 0; i < n; ++i) if (bytes[i] != expected[i]) return false;
  Frame decoded{};
  return decode(expected, sizeof(expected), decoded) == Error::Ok && same(decoded.stamp, report().stamp)
      && decoded.door == DoorState::Open;
}
constexpr bool roundTrips() {
  uint8_t bytes[kMaxFrameSize] = {}; size_t n = 0; Frame out{};
  for (uint8_t t = 1; t <= 3; ++t) for (uint8_t s = 0; s <= 2; ++s) {
    Frame f = report(static_cast<Type>(t)); f.door = static_cast<DoorState>(s);
    if (encode(f, bytes, sizeof(bytes), n) != Error::Ok || n != 17) return false;
    if (decode(bytes, n, out) != Error::Ok || out.type != f.type || out.door != f.door) return false;
  }
  for (uint8_t s = 0; s <= 4; ++s) {
    Frame f = sync(); f.system = static_cast<SystemState>(s); f.revision = s ? 11 : 0;
    if (encode(f, bytes, sizeof(bytes), n) != Error::Ok || n != 30) return false;
    if (bytes[16] != 7 || bytes[20] != 8 || bytes[24] != f.revision || bytes[28] != s || bytes[29] != 6) return false;
    if (decode(bytes, n, out) != Error::Ok || out.system != f.system || out.revision != f.revision
        || !same(out.acknowledged, f.acknowledged) || out.channel != f.channel) return false;
  }
  return true;
}
constexpr bool malformed() {
  uint8_t bytes[kMaxFrameSize + 1] = {}; size_t n = 0;
  Frame f = sync(), out = report();
  if (encode(f, bytes, sizeof(bytes), n) != Error::Ok) return false;
  for (size_t len = 0; len < n; ++len) if (decode(bytes, len, out) == Error::Ok) return false;
  if (decode(bytes, n + 1, out) != Error::Length) return false;
  if (decode(nullptr, n, out) != Error::Buffer) return false;
  // Every counterexample must leave the caller's last good object untouched.
  const size_t offsets[] = {0,2,3,4,5,14,15,28,29};
  const uint8_t invalid[] = {0,2,99,2,1,13,1,99,0};
  for (size_t i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
    const uint8_t saved = bytes[offsets[i]]; bytes[offsets[i]] = invalid[i];
    if (decode(bytes, n, out) == Error::Ok || !same(out.stamp, report().stamp) || out.type != Type::Hello) return false;
    bytes[offsets[i]] = saved;
  }
  const size_t counterOffsets[] = {6,10,16,20};
  for (size_t offset : counterOffsets) {
    const uint32_t saved = get32(bytes + offset); put32(bytes + offset, 0);
    if (decode(bytes, n, out) != Error::Counter) return false;
    put32(bytes + offset, saved);
  }
  put32(bytes + 24, 0);
  if (decode(bytes, n, out) != Error::Payload) return false;
  bytes[28] = 0; put32(bytes + 24, 1);
  if (decode(bytes, n, out) != Error::Payload) return false;
  f = report(); encode(f, bytes, sizeof(bytes), n); bytes[16] = 3;
  return decode(bytes, n, out) == Error::Payload;
}
constexpr bool outputBounds() {
  uint8_t bytes[kMaxFrameSize] = {}; size_t n = 99;
  for (size_t capacity = 0; capacity < 30; ++capacity) {
    if (encode(sync(), bytes, capacity, n) != Error::Buffer || n != 0 || bytes[0] != 0) return false;
  }
  if (encode(sync(), nullptr, 30, n) != Error::Buffer) return false;
  Frame f = report(); f.stamp.boot = 0;
  if (encode(f, bytes, sizeof(bytes), n) != Error::Counter || bytes[0] != 0) return false;
  return true;
}
constexpr bool ordering() {
  PeerTracker p{};
  if (p.online(0) || p.accept({0,1},0) != Order::Invalid) return false;
  if (p.accept({5,10},100) != Order::First || !p.online(100)) return false;
  if (p.accept({5,10},1000) != Order::Duplicate || p.receivedAt != 100) return false;
  if (p.accept({5,9},2000) != Order::Stale || p.receivedAt != 100) return false;
  if (!p.online(15099) || p.online(15100)) return false;
  if (p.accept({5,11},16000) != Order::Fresh || !p.online(16000)) return false;
  if (p.accept({6,1},17000) != Order::NewSession) return false;
  if (p.accept({5,100},18000) != Order::Stale) return false;
  PeerTracker wrap{};
  wrap.accept({UINT32_MAX,UINT32_MAX},UINT32_MAX-100);
  if (wrap.accept({UINT32_MAX,1},UINT32_MAX-50) != Order::Fresh) return false;
  if (!wrap.online(100) || wrap.online(15000)) return false;
  if (wrap.accept({1,1},15001) != Order::NewSession) return false;
  return !newer(0x80000001UL,1) && !newer(1,0x80000001UL)
      && nextCounter(UINT32_MAX) == 1 && nextCounter(0) == 1;
}
constexpr bool indicators() {
  for (uint8_t s = 0; s <= 4; ++s) {
    if (ledOn(static_cast<SystemState>(s),false)) return false;
    if (ledOn(static_cast<SystemState>(s),true) != (s == 3 || s == 4)) return false;
  }
  return true;
}
static_assert(golden(), "Wire format must match golden bytes");
static_assert(roundTrips(), "All messages and states must round trip");
static_assert(malformed(), "Malformed frames must not update state");
static_assert(outputBounds(), "Encoding must respect capacity");
static_assert(ordering(), "Ordering, sessions and offline timing must be safe");
static_assert(indicators(), "LED requires confirmed active state and live hub");
static_assert(correlates(sync(), {7,8}, {}), "Pending report can be acknowledged");
static_assert(correlates(sync(), {}, {7,8}), "Periodic sync can refer to confirmed report");
static_assert(!correlates(sync(), {8,8}, {7,7}), "Wrong boot or sequence cannot acknowledge");
static_assert(!correlates(report(), report().stamp, {}), "Reports are not acknowledgements");
static_assert(kMaxFrameSize <= 250, "Keep compatibility with ESP-NOW v1 payload limits");
