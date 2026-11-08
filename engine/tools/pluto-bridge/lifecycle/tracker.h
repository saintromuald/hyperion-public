#pragma once

// Standalone unit lifecycle/identity tracking for pluto-bridge.
//
// The state transitions mirror BWAPI 4.4.0's BWAPI/Source/BWAPI/GameUnits.cpp:
// accessible live units produce Create, Complete, Discover, and Show/Hide;
// leaving accessibility produces Hide/Evade; dying while accessible produces
// Hide/Evade/Destroy; Morph and Renegade follow in the secondary pass.  There
// is one deliberate transport adaptation: frame zero only publishes the
// bridge's existing initial Discover events, while genuine BWAPI's initial
// neutral accessibility and create-time completion rules are preserved.

#include <array>
#include <cstddef>
#include <cstdint>

namespace pluto_bridge {
namespace lifecycle {

constexpr std::size_t kMaxSlots = 1700;
constexpr int kDefaultPackedCapacity = 10000;

// Values are BWAPI 4.4.0's EventType::Enum values.  Keeping the wire values
// here avoids a BWAPI dependency in the native tests; pluto-bridge casts them
// back to BWAPI::EventType::Enum when filling GameData.
enum class EventType : int {
  UnitDiscover = 8,
  UnitEvade = 9,
  UnitShow = 10,
  UnitHide = 11,
  UnitCreate = 12,
  UnitDestroy = 13,
  UnitMorph = 14,
  UnitRenegade = 15,
  UnitComplete = 17,
};

struct UnitInput {
  std::uint32_t slot = 0;
  bool present = false;
  std::uint8_t generation = 0;
  bool visible = false;
  bool detected = false;
  bool completed = false;
  std::uint32_t type = 0;
  std::uint32_t player = 0;
};

struct Event {
  EventType type = EventType::UnitCreate;
  int unit_id = -1;

  bool operator==(const Event &other) const {
    return type == other.type && unit_id == other.unit_id;
  }
};

struct UpdateReport {
  std::size_t emitted = 0;
  std::size_t dropped = 0;
  std::size_t invalid_inputs = 0;
  std::size_t packed_id_overflow = 0;
};

class Tracker {
 public:
  explicit Tracker(int packed_capacity = kDefaultPackedCapacity)
      : packed_capacity_(packed_capacity > 0 ? packed_capacity : 0) {
    Reset();
  }

  void Reset() {
    for (SlotState &state : slots_)
      state = SlotState();
    next_packed_id_ = 0;
  }

  // Returns the current logical unit's permanent packed BWAPI id, or -1 for an
  // invalid slot, an absent unit, or packed-id-space exhaustion.
  int PackedId(std::size_t slot) const {
    return slot < kMaxSlots && slots_[slot].present ? slots_[slot].packed_id : -1;
  }

  bool Detected(std::size_t slot) const {
    return slot < kMaxSlots && slots_[slot].present && slots_[slot].detected;
  }

  // Consume one complete frame.  Slots omitted from inputs are deaths, as are
  // explicit inputs with present=false.  The first occurrence of a duplicate
  // slot wins.  Normal event phases use ascending slot order; the initial
  // discovery compatibility pass retains stream order.  Packed ids are always
  // allocated in first-appearance stream order.
  //
  // At most output_capacity events are written.  Further events are counted as
  // dropped, never written out of bounds, and are not replayed on later frames.
  // Lifecycle state still advances so a small destination cannot corrupt or
  // stall the tracker.  Passing nullptr is valid only with zero capacity.
  UpdateReport Update(const UnitInput *inputs, std::size_t input_count,
                      bool initial_frame, Event *output,
                      std::size_t output_capacity) {
    UpdateReport report;
    if (output == nullptr)
      output_capacity = 0;

    std::array<UnitInput, kMaxSlots> current{};
    std::array<bool, kMaxSlots> supplied{};
    std::array<std::size_t, kMaxSlots> order{};
    std::size_t order_count = 0;

    if (inputs == nullptr)
      input_count = 0;
    for (std::size_t i = 0; i < input_count; ++i) {
      const UnitInput &input = inputs[i];
      if (input.slot >= kMaxSlots) {
        ++report.invalid_inputs;
        continue;
      }
      const std::size_t slot = static_cast<std::size_t>(input.slot);
      if (supplied[slot]) {
        ++report.invalid_inputs;
        continue;
      }
      supplied[slot] = true;
      current[slot] = input;
      order[order_count++] = slot;
    }

    std::array<bool, kMaxSlots> fresh{};
    std::array<int, kMaxSlots> fresh_packed{};
    fresh_packed.fill(-1);
    for (std::size_t i = 0; i < order_count; ++i) {
      const std::size_t slot = order[i];
      const UnitInput &input = current[slot];
      if (!input.present)
        continue;
      const SlotState &old = slots_[slot];
      if (old.present && old.generation == input.generation)
        continue;
      fresh[slot] = true;
      if (next_packed_id_ < packed_capacity_) {
        fresh_packed[slot] = next_packed_id_++;
      } else {
        ++report.packed_id_overflow;
      }
    }

    auto emit = [&](EventType type, int unit_id) {
      if (unit_id < 0) {
        ++report.dropped;
        return;
      }
      if (report.emitted < output_capacity) {
        output[report.emitted++] = Event{type, unit_id};
      } else {
        ++report.dropped;
      }
    };

    auto leave = [&](SlotState &old, bool destroy) {
      if (!old.accessible)
        return;
      if (old.was_visible)
        emit(EventType::UnitHide, old.packed_id);
      emit(EventType::UnitEvade, old.packed_id);
      if (destroy)
        emit(EventType::UnitDestroy, old.packed_id);
    };

    // A native slot can be reused without an intervening empty frame.  Retire
    // the old accessible identity before any event for its replacement.  This
    // is the one ordering addition to GameUnits.cpp's normal live/dead passes.
    for (std::size_t slot = 0; slot < kMaxSlots; ++slot) {
      SlotState &old = slots_[slot];
      if (old.present && fresh[slot]) {
        leave(old, true);
        ClearLifetime(old);
      }
    }

    // Primary live-unit pass.  Deaths are held for the following pass, as in
    // GameUnits.cpp, while fog evasion is handled here.
    const std::size_t primary_count = initial_frame ? order_count : kMaxSlots;
    for (std::size_t primary = 0; primary < primary_count; ++primary) {
      const std::size_t slot = initial_frame ? order[primary] : primary;
      SlotState &state = slots_[slot];
      const UnitInput &input = current[slot];
      const bool present = supplied[slot] && input.present;
      if (!present)
        continue;

      if (fresh[slot]) {
        ClearLifetime(state);
        state.present = true;
        state.generation = input.generation;
        state.packed_id = fresh_packed[slot];
      }
      state.detected = input.detected;

      const bool accessible = input.visible || (initial_frame && input.player == 11);
      if (initial_frame) {
        // onStart already receives initial UnitData.  Preserve the bridge's
        // initial discovery behavior without synthetic Create/Show/Complete or
        // secondary-pass events.
        state.was_alive = true;
        state.accessible = accessible;
        state.was_visible = accessible && input.visible;
        // A hidden unit has not supplied an accessible completion baseline.
        // When it later becomes accessible, GameUnits.cpp observes its real
        // completed flag and emits UnitComplete rather than suppressing it.
        state.was_completed = accessible && input.completed;
        if (accessible) {
          emit(EventType::UnitDiscover, state.packed_id);
          state.last_type = input.type;
          state.last_player = input.player;
          state.has_last_type = true;
          state.has_last_player = true;
        }
        continue;
      }

      if (accessible) {
        const bool newly_alive = !state.was_alive;
        if (newly_alive)
          emit(EventType::UnitCreate, state.packed_id);
        if (!state.was_completed && input.completed)
          emit(EventType::UnitComplete, state.packed_id);
        if (input.completed)
          state.was_completed = true;
        if (!state.accessible)
          emit(EventType::UnitDiscover, state.packed_id);
        if (!state.was_visible)
          emit(EventType::UnitShow, state.packed_id);
      } else if (state.accessible) {
        leave(state, false);
      }

      state.was_alive = true;
      state.accessible = accessible;
      state.was_visible = accessible && input.visible;
    }

    // Dying units only generate events if they were still accessible.  Thus a
    // unit that already evaded into fog does not generate a second Evade or a
    // later invisible Destroy, matching GameUnits.cpp's dyingUnits loop.
    for (std::size_t slot = 0; slot < kMaxSlots; ++slot) {
      SlotState &state = slots_[slot];
      const bool present = supplied[slot] && current[slot].present;
      if (state.present && !present) {
        leave(state, true);
        ClearLifetime(state);
      }
    }

    if (!initial_frame) {
      // Secondary accessible-unit pass: Morph precedes Renegade.  New logical
      // units establish a baseline rather than morphing from their predecessor.
      for (std::size_t slot = 0; slot < kMaxSlots; ++slot) {
        SlotState &state = slots_[slot];
        const UnitInput &input = current[slot];
        if (!state.present || !state.accessible)
          continue;
        if (state.has_last_type && state.last_type != input.type) {
          emit(EventType::UnitMorph, state.packed_id);
          // GameUnits.cpp resets wasCompleted after a morph.  A completed new
          // type therefore generates Complete on the following frame.
          state.was_completed = false;
        }
        if (state.has_last_player && state.last_player != input.player)
          emit(EventType::UnitRenegade, state.packed_id);
        state.last_type = input.type;
        state.last_player = input.player;
        state.has_last_type = true;
        state.has_last_player = true;
      }
    }

    return report;
  }

 private:
  struct SlotState {
    bool present = false;
    std::uint8_t generation = 0;
    int packed_id = -1;
    bool detected = false;
    bool was_alive = false;
    bool accessible = false;
    bool was_visible = false;
    bool was_completed = false;
    std::uint32_t last_type = 0;
    std::uint32_t last_player = 0;
    bool has_last_type = false;
    bool has_last_player = false;
  };

  static void ClearLifetime(SlotState &state) {
    state = SlotState();
  }

  std::array<SlotState, kMaxSlots> slots_{};
  int packed_capacity_ = kDefaultPackedCapacity;
  int next_packed_id_ = 0;
};

}  // namespace lifecycle
}  // namespace pluto_bridge
