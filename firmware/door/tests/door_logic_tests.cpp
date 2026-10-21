// Compile with C++14 or later, -fsyntax-only: every scenario runs in static_assert.
#include "../door_logic.h"
using namespace Domotica;

constexpr Frame sync(Stamp hub, Stamp ack, SystemState state = SystemState::Armed,
                     uint32_t revision = 1, uint8_t channel = 6) {
  Frame f{}; f.type = Type::StateSync; f.source = Node::Hub; f.destination = Node::Door;
  f.stamp = hub; f.acknowledged = ack; f.system = state; f.revision = revision; f.channel = channel;
  return f;
}

constexpr bool debounce() {
  Door::Debouncer d;
  if (d.sample(false, 100, 40) || d.ready || d.sample(false, 139, 40)) return false;
  if (!d.sample(false, 140, 40) || d.state(true, false) != DoorState::Closed) return false;
  if (d.state(false, false) != DoorState::Unknown) return false;
  if (d.sample(true, 200, 40) || d.sample(false, 220, 40) || d.sample(false, 260, 40)) return false;
  if (d.sample(true, 300, 40) || d.sample(true, 339, 40) || !d.sample(true, 340, 40)) return false;
  if (d.state(true, false) != DoorState::Open || d.state(true, true) != DoorState::Closed) return false;
  Door::Debouncer wrap;
  return !wrap.sample(true, UINT32_MAX - 19, 40) && !wrap.sample(true, 19, 40)
      && wrap.sample(true, 20, 40);
}
static_assert(debounce(), "initial filter, bounce, polarity, millis wrap");

constexpr bool queues() {
  Door::Events q;
  for (size_t i = 0; i < kEventQueueDepth; ++i) q.push(DoorState::Closed);
  q.push(DoorState::Open);
  if (q.count != 1 || q.lost != 8 || q.pop() != DoorState::Open) return false;
  q.push(DoorState::Open); q.push(DoorState::Closed);
  q.clear();
  return q.count == 0 && q.lost == 10;
}
static_assert(queues(), "overflow keeps newest and counts discarded edges; search clears backlog");

constexpr bool heartbeatOrder() {
  Door::Events q; Type type{}; DoorState state{};
  q.push(DoorState::Open); q.push(DoorState::Closed);
  q.heartbeat(DoorState::Closed);
  q.heartbeat(DoorState::Open); // Redundant heartbeat must not replace its snapshot.
  q.push(DoorState::Open);
  if (!q.take(type, state) || type != Type::Event || state != DoorState::Open) return false;
  q.push(DoorState::Closed); // Continuous input cannot starve the heartbeat.
  if (!q.take(type, state) || type != Type::Event || state != DoorState::Closed) return false;
  if (!q.take(type, state) || type != Type::Status || state != DoorState::Closed) return false;
  if (!q.take(type, state) || type != Type::Event || state != DoorState::Open) return false;
  if (!q.take(type, state) || state != DoorState::Closed || q.take(type, state)) return false;
  for (size_t i = 0; i < kEventQueueDepth; ++i) q.push(DoorState::Closed);
  q.heartbeat(DoorState::Closed); q.push(DoorState::Open); // Overflow across fence.
  if (!q.take(type, state) || type != Type::Status || state != DoorState::Closed) return false;
  return q.take(type, state) && type == Type::Event && state == DoorState::Open && q.lost == 8;
}
static_assert(heartbeatOrder(), "FIFO snapshots across STATUS; bounded heartbeat; overflow ordering");

constexpr bool retries() {
  Door::Pending p;
  if (!p.start(Type::Event, DoorState::Open, {7, 9}, 100) || !p.due(100)) return false;
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
      || decoded.door != DoorState::Open) return false;
  p.start(Type::Hello, DoorState::Unknown, {8, 1}, UINT32_MAX - 99);
  return !p.expired(649) && p.expired(650);
}
static_assert(retries(), "0/250/500 attempts, 750ms deadline, identical bytes, wrap");

constexpr bool acknowledgements() {
  Door::Link l; Door::Pending p;
  p.start(Type::Hello, DoorState::Closed, {10,1}, 0);
  if (l.led(0) || l.accept(sync({20,1}, {9,1}), p, 6, 6, 1) || l.hub.seen) return false;
  if (l.accept(sync({20,1}, {10,1}), p, 1, 6, 1) || l.hub.seen) return false;
  if (l.accept(sync({20,1}, {10,1}), p, 6, 1, 1) || l.hub.seen) return false;
  if (!l.accept(sync({20,1}, {10,1}), p, 6, 6, 10) || p.active || !l.led(10)) return false;
  p.start(Type::Event, DoorState::Open, {10,2}, 20);
  // A new periodic sync of the last confirmed report is valid, but not ACK of pending.
  if (!l.accept(sync({20,2}, {10,1}), p, 6, 6, 30) || !p.active) return false;
  if (l.accept(sync({20,2}, {10,2}), p, 6, 6, 40) || !p.active || l.hub.receivedAt != 30) return false;
  if (l.accept(sync({19,100}, {10,2}), p, 6, 6, 40) || !p.active) return false;
  if (!l.accept(sync({21,1}, {10,2}, SystemState::Unknown, 0), p, 6, 6, 50)
      || p.active || l.led(50)) return false;
  // A timed-out report is no longer a valid ACK target.
  p.start(Type::Status, DoorState::Open, {10,3}, 60);
  if (l.accept(sync({21,2}, {10,3}), p, 6, 6, 810)) return false;
  p.active = false;
  if (l.accept(sync({21,2}, {10,3}), p, 6, 6, 900) || l.hub.receivedAt != 50) return false;
  return !l.accept(sync({21,2}, {10,2}, SystemState::Armed, 0), p, 6, 6, 901);
}
static_assert(acknowledgements(), "correlation before tracker, pending vs confirmed, hub reboot, malformed");

constexpr bool liveness() {
  Door::Link l; Door::Pending p;
  p.start(Type::Hello, DoorState::Open, {1,1}, UINT32_MAX - 99);
  if (!l.accept(sync({1,1}, {1,1}), p, 6, 6, UINT32_MAX - 99)) return false;
  if (!l.led(14899) || l.led(14900)) return false;
  if (l.accept(sync({1,1}, {1,1}), p, 6, 6, 15000) || l.led(15000)) return false;
  if (!l.accept(sync({1,2}, {1,1}, SystemState::Alarm), p, 6, 6, 15001) || !l.led(15001)) return false;
  if (!l.accept(sync({1,3}, {1,1}, SystemState::ExitDelay), p, 6, 6, 15002) || l.led(15002)) return false;
  return l.accept(sync({1,4}, {1,1}, SystemState::Disarmed), p, 6, 6, 15003) && !l.led(15003);
}
static_assert(liveness(), "15s exact timeout, millis wrap, replay cannot revive link, confirmed LED only");
