// Compile with C++14 or later, -fsyntax-only: every scenario runs in static_assert.
#include "../pir_link.h"
#include "../pir_logic.h"
using namespace Domotica;

constexpr Frame sync(Stamp hub, Stamp ack, SystemState state = SystemState::Armed,
                     uint32_t revision = 1, uint8_t channel = 6) {
  Frame f{}; f.type = Type::StateSync; f.source = Node::Hub; f.destination = Node::Pir;
  f.stamp = hub; f.acknowledged = ack; f.system = state; f.revision = revision; f.channel = channel;
  return f;
}

constexpr bool queues() {
  Pir::Events q;
  for (size_t i = 0; i < kEventQueueDepth; ++i) q.push(PirState::NoMotion);
  q.push(PirState::Motion);
  if (q.count != 1 || q.lost != 8 || q.pop() != PirState::Motion) return false;
  q.push(PirState::Motion); q.push(PirState::NoMotion);
  q.clear();
  return q.count == 0 && q.lost == 10;
}
static_assert(queues(), "overflow keeps newest and counts discarded edges; search clears backlog");

constexpr bool heartbeatOrder() {
  Pir::Events q; Type type{}; PirState state{};
  q.push(PirState::Motion); q.push(PirState::NoMotion);
  q.heartbeat(PirState::NoMotion);
  q.heartbeat(PirState::Motion); // Redundant heartbeat must not replace its snapshot.
  q.push(PirState::Motion);
  if (!q.take(type, state) || type != Type::PirEvent || state != PirState::Motion) return false;
  q.push(PirState::NoMotion); // Continuous input cannot starve the heartbeat.
  if (!q.take(type, state) || type != Type::PirEvent || state != PirState::NoMotion) return false;
  if (!q.take(type, state) || type != Type::PirStatus || state != PirState::NoMotion) return false;
  if (!q.take(type, state) || type != Type::PirEvent || state != PirState::Motion) return false;
  if (!q.take(type, state) || state != PirState::NoMotion || q.take(type, state)) return false;
  for (size_t i = 0; i < kEventQueueDepth; ++i) q.push(PirState::NoMotion);
  q.heartbeat(PirState::NoMotion); q.push(PirState::Motion); // Overflow across fence.
  if (!q.take(type, state) || type != Type::PirStatus || state != PirState::NoMotion) return false;
  return q.take(type, state) && type == Type::PirEvent && state == PirState::Motion && q.lost == 8;
}
static_assert(heartbeatOrder(), "FIFO snapshots across STATUS; bounded heartbeat; overflow ordering");

constexpr bool retries() {
  Pir::Pending p;
  if (!p.start(Type::PirEvent, PirState::Motion, {7, 9}, 100) || !p.due(100)) return false;
  uint8_t original[kMaxFrameSize]{};
  for (size_t i = 0; i < p.length; ++i) original[i] = p.bytes[i];
  ++p.attempts;
  if (p.due(349) || !p.due(350)) return false;
  ++p.attempts;
  if (p.due(599) || !p.due(600)) return false;
  ++p.attempts;
  if (p.due(601) || p.expired(849) || !p.expired(850) || p.due(850)) return false;
  for (size_t i = 0; i < p.length; ++i) if (p.bytes[i] != original[i]) return false;
  Frame decoded{};
  if (decode(p.bytes, p.length, decoded) != Error::Ok || !same(decoded.stamp, {7,9})
      || decoded.pir != PirState::Motion) return false;
  p.start(Type::PirHello, PirState::Unknown, {8, 1}, UINT32_MAX - 99);
  return !p.expired(649) && p.expired(650);
}
static_assert(retries(), "0/250/500 attempts, 750ms deadline, identical bytes, wrap");

constexpr bool acknowledgements() {
  Pir::Link l; Pir::Pending p;
  p.start(Type::PirHello, PirState::NoMotion, {10,1}, 0);
  if (l.led(0) || l.accept(sync({20,1}, {9,1}), p, 6, 6, 1) || l.hub.seen) return false;
  if (l.accept(sync({20,1}, {10,1}), p, 1, 6, 1) || l.hub.seen) return false;
  if (l.accept(sync({20,1}, {10,1}), p, 6, 1, 1) || l.hub.seen) return false;
  if (!l.accept(sync({20,1}, {10,1}), p, 6, 6, 10) || p.active || !l.led(10)) return false;
  p.start(Type::PirEvent, PirState::Motion, {10,2}, 20);
  // A new periodic sync of the last confirmed report is valid, but not ACK of pending.
  if (!l.accept(sync({20,2}, {10,1}), p, 6, 6, 30) || !p.active) return false;
  if (l.accept(sync({20,2}, {10,2}), p, 6, 6, 40) || !p.active || l.hub.receivedAt != 30) return false;
  if (l.accept(sync({19,100}, {10,2}), p, 6, 6, 40) || !p.active) return false;
  if (!l.accept(sync({21,1}, {10,2}, SystemState::Unknown, 0), p, 6, 6, 50)
      || p.active || l.led(50)) return false;
  // A timed-out report is no longer a valid ACK target.
  p.start(Type::PirStatus, PirState::Motion, {10,3}, 60);
  if (l.accept(sync({21,2}, {10,3}), p, 6, 6, 810)) return false;
  p.active = false;
  if (l.accept(sync({21,2}, {10,3}), p, 6, 6, 900) || l.hub.receivedAt != 50) return false;
  return !l.accept(sync({21,2}, {10,2}, SystemState::Armed, 0), p, 6, 6, 901);
}
static_assert(acknowledgements(), "correlation before tracker, pending vs confirmed, hub reboot, malformed");

constexpr bool liveness() {
  Pir::Link l; Pir::Pending p;
  p.start(Type::PirHello, PirState::Motion, {1,1}, UINT32_MAX - 99);
  if (!l.accept(sync({1,1}, {1,1}), p, 6, 6, UINT32_MAX - 99)) return false;
  if (!l.led(14899) || l.led(14900)) return false;
  if (l.accept(sync({1,1}, {1,1}), p, 6, 6, 15000) || l.led(15000)) return false;
  if (!l.accept(sync({1,2}, {1,1}, SystemState::Alarm), p, 6, 6, 15001) || !l.led(15001)) return false;
  if (!l.accept(sync({1,3}, {1,1}, SystemState::ExitDelay), p, 6, 6, 15002) || l.led(15002)) return false;
  return l.accept(sync({1,4}, {1,1}, SystemState::Disarmed), p, 6, 6, 15003) && !l.led(15003);
}
static_assert(liveness(), "15s exact timeout, millis wrap, replay cannot revive link, confirmed LED only");

constexpr bool crossedAckAndRestart() {
  Pir::Link l; Pir::Pending p;
  p.start(Type::PirHello, PirState::NotReady, {3,1}, 0);
  auto f = sync({4,1}, {3,1});
  f.destination = Node::Door; // Same counters are valid for another node, never for PIR.
  if (l.accept(f, p, 6, 6, 10) || l.hub.seen || !p.active) return false;
  f.destination = Node::Pir;
  if (!l.accept(f, p, 6, 6, 11) || !l.led(11)) return false;
  // Global hub sequences can skip values used for door.
  if (!l.accept(sync({4,100}, {3,1}, SystemState::Disarmed, 2), p, 6, 6, 20)
      || l.led(20) || l.revision != 2) return false;
  if (l.accept(sync({4,99}, {3,1}), p, 6, 6, 21) || l.hub.receivedAt != 20) return false;
  // A new hub session may restart its global revision.
  if (!l.accept(sync({5,1}, {3,1}, SystemState::Disarmed, 1), p, 6, 6, 30)) return false;
  Pir::Link restarted; Pir::Pending fresh;
  fresh.start(Type::PirHello, PirState::Unknown, {4,1}, 40);
  if (restarted.accept(sync({5,2}, {3,1}), fresh, 6, 6, 41) || restarted.hub.seen) return false;
  return restarted.accept(sync({5,3}, {4,1}), fresh, 6, 6, 42) && !fresh.active;
}
static_assert(crossedAckAndRestart(), "Reject door ACK, tolerate hub sequence gaps, recover both reboots");

constexpr bool warmupDelivery() {
  Pir::Sensor sensor; Pir::Pending pending; Pir::Events events; Pir::Link link;
  sensor.sample(true, 0, 1000, 20);
  if (!pending.start(Type::PirHello, sensor.wireState(true, true), {7,1}, 0)) return false;
  Frame decoded{};
  if (decode(pending.bytes, pending.length, decoded) != Error::Ok
      || decoded.source != Node::Pir || decoded.destination != Node::Hub
      || decoded.type != Type::PirHello || decoded.pir != PirState::NotReady) return false;
  if (!link.accept(sync({8,1}, {7,1}), pending, 6, 6, 10)
      || !link.led(10) || sensor.ready) return false; // LED independent of readiness.
  pending.start(Type::PirStatus, PirState::NotReady, {7,2}, 900);
  sensor.sample(true, 1000, 1000, 20);
  if (!sensor.sample(true, 1020, 1000, 20)) return false;
  events.push(sensor.wireState(true, true));
  for (uint32_t now = 1021; now < 1100; ++now)
    if (sensor.sample(true, now, 1000, 20)) events.push(sensor.wireState(true, true));
  if (events.count != 1 || !pending.active) return false;
  if (!link.accept(sync({8,3}, {7,2}), pending, 6, 6, 1100)) return false;
  Type type{}; PirState state{};
  if (!events.take(type, state) || type != Type::PirEvent || state != PirState::Motion) return false;
  if (!pending.start(type, state, {7,3}, 1100)) return false;
  return decode(pending.bytes, pending.length, decoded) == Error::Ok
      && decoded.type == Type::PirEvent && decoded.pir == PirState::Motion
      && events.count == 0;
}
static_assert(warmupDelivery(), "Link during warmup, first MOTION queued behind pending, held level only once");
