#pragma once

// The HXP3 visibility reduction mirrors BWAPI 4.4.0's server-side
// UnitImpl::updateInternalData (UnitUpdate.cpp).  Hyperion transports the raw
// sprite visibility and detector bit separately; BWAPI visibility is not simply
// their conjunction for burrowed units.

#include <cstdint>

namespace pluto_bridge {

struct UnitVisibilityInput {
  bool raw_visible = false;
  bool own_unit = false;
  bool has_hxp3 = false;
  std::uint32_t status_flags = 0;
  std::uint32_t order = 0;
  std::uint8_t movement_flags = 0;
  std::uint8_t ground_cooldown = 0;
  std::uint8_t air_cooldown = 0;
  bool detected_by_us = false;
};

struct UnitVisibility {
  bool visible = false;
  bool detected = false;
};

inline UnitVisibility EffectiveUnitVisibility(const UnitVisibilityInput &input) {
  UnitVisibility result;
  result.visible = input.raw_visible;
  if (!input.has_hxp3 || input.own_unit) {
    // Legacy streams have no status/detector fields.  Own units are always
    // detected when visible, as in UnitUpdate.cpp's self-player branch.
    result.detected = result.visible;
    return result;
  }

  constexpr std::uint32_t kBurrowed = 0x00000010;
  constexpr std::uint32_t kRequiresDetection = 0x00000100;
  constexpr std::uint8_t kAccelerating = 0x02;
  constexpr std::uint8_t kMoving = 0x10;
  constexpr std::uint32_t kMoveOrder = 6;  // BWAPI::Orders::Enum::Move

  const bool requires_detection = (input.status_flags & kRequiresDetection) != 0;
  if (requires_detection && !input.detected_by_us) {
    const bool moving = (input.movement_flags & (kMoving | kAccelerating)) != 0 ||
                        input.order == kMoveOrder;
    const bool firing = input.ground_cooldown != 0 || input.air_cooldown != 0;
    const bool burrowed = (input.status_flags & kBurrowed) != 0;
    // A stationary, idle, burrowed detector-required enemy disappears despite
    // its raw sprite bit.  Movement and weapon cooldown temporarily reveal it;
    // non-burrowed cloaked units retain visible-but-undetected accessibility.
    result.visible = result.visible && (moving || firing || !burrowed);
  }
  result.detected = result.visible && (!requires_detection || input.detected_by_us);
  return result;
}

}  // namespace pluto_bridge
