#pragma once
#include <DomoticaProtocol.h>

// PIR delivery state; wire encoding belongs exclusively to DomoticaProtocol.
namespace Pir {
using namespace Domotica;

struct Events {
  PirState data[kEventQueueDepth]{};
  size_t head = 0, count = 0;
  uint32_t lost = 0;
  bool heartbeatReady = false;
  size_t beforeHeartbeat = 0;
  PirState heartbeatSnapshot = PirState::Unknown;
  constexpr void clearChanges() { lost += count; head = 0; count = 0; beforeHeartbeat = 0; }
  constexpr void clear() { clearChanges(); heartbeatReady = false; }
  constexpr void push(PirState state) {
    if (count == kEventQueueDepth) clearChanges();
    data[(head + count) % kEventQueueDepth] = state; ++count;
  }
  // Keep at most one heartbeat snapshot, ordered after already queued changes.
  // New edges cannot starve STATUS or make an old EVENT overwrite a newer STATUS.
  constexpr void heartbeat(PirState state) {
    if (heartbeatReady) return;
    heartbeatReady = true; beforeHeartbeat = count; heartbeatSnapshot = state;
  }
  constexpr PirState pop() {
    if (!count) return PirState::Unknown;
    const PirState s = data[head]; head = (head + 1) % kEventQueueDepth; --count;
    return s;
  }
  constexpr bool take(Type& type, PirState& state) {
    if (heartbeatReady && beforeHeartbeat == 0) {
      heartbeatReady = false; type = Type::PirStatus; state = heartbeatSnapshot; return true;
    }
    if (!count) return false;
    type = Type::PirEvent; state = pop();
    if (heartbeatReady && beforeHeartbeat) --beforeHeartbeat;
    return true;
  }
};

struct Pending {
  bool active = false;
  Stamp stamp{};
  uint8_t bytes[kMaxFrameSize]{};
  size_t length = 0;
  uint32_t startedAt = 0;
  uint8_t attempts = 0;
  constexpr bool start(Type type, PirState sensor, Stamp id, uint32_t now) {
    Frame frame{}; frame.type = type; frame.source = Node::Pir; frame.destination = Node::Hub; frame.pir = sensor; frame.stamp = id;
    if (encode(frame, bytes, sizeof(bytes), length) != Error::Ok) return false;
    stamp = id; startedAt = now; attempts = 0; active = true; return true;
  }
  constexpr bool expired(uint32_t now) const {
    return active && elapsed(now, startedAt, kAckTimeoutMs * kMaxAttempts);
  }
  constexpr bool due(uint32_t now) const {
    return active && !expired(now) && attempts < kMaxAttempts
        && elapsed(now, startedAt, uint32_t(attempts) * kAckTimeoutMs);
  }
};

struct Link {
  PeerTracker hub{}; // Kept across offline transitions and channel sweeps.
  Stamp confirmed{};
  SystemState system = SystemState::Unknown;
  uint32_t revision = 0;
  constexpr bool accept(const Frame& f, Pending& pending, uint8_t localChannel,
                        uint8_t rxChannel, uint32_t receivedAt) {
    const Stamp pendingId = pending.active && !pending.expired(receivedAt) ? pending.stamp : Stamp{};
    if (validate(f) != Error::Ok || f.channel != localChannel || rxChannel != localChannel
        || !correlates(f, pendingId, confirmed, Node::Pir)) return false;
    const Order order = hub.classify(f.stamp);
    if (order != Order::First && order != Order::NewSession && order != Order::Fresh) return false;
    hub.accept(f.stamp, receivedAt);
    system = f.system; revision = f.revision;
    if (pending.active && same(f.acknowledged, pending.stamp)) {
      confirmed = pending.stamp; pending.active = false;
    }
    return true;
  }
  constexpr bool led(uint32_t now) const { return ledOn(system, hub.online(now)); }
};
}
