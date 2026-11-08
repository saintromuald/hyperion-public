#pragma once

// Client-driven shared-memory publication sequence.  All methods are called
// with the bridge state lock held.  Generation zero means "no update delivered
// yet"; the initial code 2 that only releases Client::connect is not a delivery.

#include <cstdint>

namespace pluto_bridge {

class PublicationState {
 public:
  void Publish() { ++ready_; }
  bool CanWriteNext() const { return !client_active_ || acknowledged_ >= ready_; }

  // An optional DLL that never connects must not stall host frames.  Once a
  // real pipe client is connected, however, its acknowledgement gates writes.
  void ClientConnected() { client_active_ = true; }
  void ClientClosed() { client_active_ = false; }
  bool client_active() const { return client_active_; }

  // A code 1 acknowledges the update most recently released with code 2.  The
  // first code 1 follows connect-only code 2 and therefore acknowledges zero.
  void AcknowledgeDelivered() { acknowledged_ = delivered_; }
  bool HasUndeliveredPublication() const { return ready_ > delivered_; }
  std::uint64_t MarkDelivered() {
    delivered_ = ready_;
    return delivered_;
  }

  std::uint64_t ready() const { return ready_; }
  std::uint64_t delivered() const { return delivered_; }
  std::uint64_t acknowledged() const { return acknowledged_; }

 private:
  std::uint64_t ready_ = 0;
  std::uint64_t delivered_ = 0;
  std::uint64_t acknowledged_ = 0;
  bool client_active_ = false;
};

}  // namespace pluto_bridge
