#include "lifecycle/tracker.h"
#include "official-dll.h"
#include "publication.h"
#include "turn-commands.h"
#include "unit-visibility.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using pluto_bridge::PublicationState;
using pluto_bridge::TurnCommandSize;
using pluto_bridge::UnitVisibility;
using pluto_bridge::UnitVisibilityInput;

void Expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

UnitVisibilityInput CloakedBurrowed() {
  UnitVisibilityInput input;
  input.raw_visible = true;
  input.has_hxp3 = true;
  input.status_flags = 0x110;  // RequiresDetection | Burrowed
  input.order = 3;             // PlayerGuard: stationary
  input.detected_by_us = false;
  return input;
}

void TestBurrowedVisibility() {
  UnitVisibilityInput input = CloakedBurrowed();
  UnitVisibility result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(!result.visible && !result.detected,
         "stationary undetected burrowed enemy is not BWAPI-visible");

  input.detected_by_us = true;
  result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(result.visible && result.detected, "detector bit reveals burrowed enemy");

  const std::array<std::uint8_t, 2> movement{{0x02, 0x10}};
  for (std::uint8_t flags : movement) {
    input = CloakedBurrowed();
    input.movement_flags = flags;
    result = pluto_bridge::EffectiveUnitVisibility(input);
    Expect(result.visible && !result.detected,
           "movement exposes burrowed enemy without fabricating detection");
  }
  input = CloakedBurrowed();
  input.order = 6;  // Move
  result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(result.visible && !result.detected, "Move order exposes burrowed enemy");

  input = CloakedBurrowed();
  input.ground_cooldown = 1;
  result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(result.visible && !result.detected, "ground cooldown exposes burrowed enemy");
  input = CloakedBurrowed();
  input.air_cooldown = 1;
  result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(result.visible && !result.detected, "air cooldown exposes burrowed enemy");

  input = CloakedBurrowed();
  input.status_flags = 0x100;  // cloaked, but not burrowed
  result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(result.visible && !result.detected,
         "ordinary cloaked enemy remains accessible while undetected");

  input.status_flags = 1;
  result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(result.visible && result.detected,
         "non-detector-required enemy does not need a detector bit");

  input = CloakedBurrowed();
  input.raw_visible = false;
  input.detected_by_us = true;
  result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(!result.visible && !result.detected, "detection cannot create raw sprite visibility");

  input = CloakedBurrowed();
  input.own_unit = true;
  result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(result.visible && result.detected, "own units use BWAPI's self-player branch");

  input = CloakedBurrowed();
  input.has_hxp3 = false;
  result = pluto_bridge::EffectiveUnitVisibility(input);
  Expect(result.visible && result.detected, "legacy streams retain legacy visibility");
}

void TestBurrowedLifecycleTransition() {
  namespace lifecycle = pluto_bridge::lifecycle;
  lifecycle::Tracker tracker;
  std::array<lifecycle::Event, 16> events{};

  UnitVisibilityInput hxp = CloakedBurrowed();
  UnitVisibility effective = pluto_bridge::EffectiveUnitVisibility(hxp);
  lifecycle::UnitInput unit;
  unit.slot = 9;
  unit.present = true;
  unit.generation = 1;
  unit.player = 1;
  unit.completed = true;
  unit.visible = effective.visible;
  unit.detected = effective.detected;
  lifecycle::UpdateReport report = tracker.Update(&unit, 1, true, events.data(), events.size());
  Expect(report.emitted == 0, "hidden burrowed frame-zero enemy has no Discover");

  hxp.detected_by_us = true;
  effective = pluto_bridge::EffectiveUnitVisibility(hxp);
  unit.visible = effective.visible;
  unit.detected = effective.detected;
  report = tracker.Update(&unit, 1, false, events.data(), events.size());
  Expect(report.emitted == 3, "detected burrowed enemy emits Complete/Discover/Show");
  Expect(events[0].type == lifecycle::EventType::UnitComplete &&
             events[1].type == lifecycle::EventType::UnitDiscover &&
             events[2].type == lifecycle::EventType::UnitShow,
         "burrowed lifecycle event order follows GameUnits.cpp");
}

void TestOfficialSha256() {
  char hex[65];
  pluto_bridge::Sha256 empty;
  empty.Finish(hex);
  Expect(std::string(hex) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
         "SHA-256 empty vector");

  pluto_bridge::Sha256 abc;
  const unsigned char bytes[] = {'a', 'b', 'c'};
  abc.Update(bytes, 1);
  abc.Update(bytes + 1, 2);
  abc.Finish(hex);
  Expect(std::string(hex) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "SHA-256 incremental abc vector");
  Expect(std::string(pluto_bridge::OfficialPlutoSha256()) ==
             "603fea2f77a4b652f29b832c2977cf5bc9d83bbcb927febd04499220020ce3bc",
         "official Pluto digest remains pinned");
}

void TestTurnCommandFraming() {
  for (unsigned char lobby : {0x3C, 0x41, 0x44, 0x3E}) {
    const std::array<unsigned char, 8> bytes{{lobby, 0, 0, 0, 0, 0, 0, 0}};
    Expect(TurnCommandSize(bytes.data(), bytes.size()) == -1,
           "lobby-only command rejected by in-game framing");
  }
  const std::array<unsigned char, 4> select{{0x09, 1, 0, 0}};
  Expect(TurnCommandSize(select.data(), select.size()) == 4,
         "zero unit id bytes remain binary-safe inside Select");
  Expect(TurnCommandSize(select.data(), 3) == 0, "truncated Select is incomplete");
  const std::array<unsigned char, 2> too_many{{0x09, 13}};
  Expect(TurnCommandSize(too_many.data(), too_many.size()) == -1,
         "oversized Select count rejected");
  const std::array<unsigned char, 1> valid{{0x5A}};
  Expect(TurnCommandSize(valid.data(), valid.size()) == 1, "valid in-game opcode retained");
  const std::array<unsigned char, 1> unknown{{0xFF}};
  Expect(TurnCommandSize(unknown.data(), unknown.size()) == -1, "unknown opcode rejected");
}

void TestPublicationSequence() {
  PublicationState state;
  state.Publish();
  Expect(state.ready() == 1 && state.CanWriteNext(),
         "frame zero can publish before an optional client connects");
  state.ClientConnected();

  // connect()'s initial code 2 delivered no generation.  The first update's
  // code 1 therefore acknowledges zero and releases publication one.
  state.AcknowledgeDelivered();
  Expect(state.acknowledged() == 0 && state.HasUndeliveredPublication(),
         "connect-only code 2 is not a frame delivery");
  Expect(state.MarkDelivered() == 1, "first client update receives frame zero");
  Expect(!state.CanWriteNext() && !state.HasUndeliveredPublication(),
         "same generation cannot be delivered twice or overwritten early");

  for (std::uint64_t generation = 2; generation <= 5; ++generation) {
    // This code 1 is sent only after the preceding Client::update consumed the
    // previous shared event array.  It opens the next host write.
    state.AcknowledgeDelivered();
    Expect(state.acknowledged() == generation - 1 && state.CanWriteNext(),
           "client acknowledgement opens exactly one next publication");
    state.Publish();
    Expect(state.ready() == generation && state.HasUndeliveredPublication(),
           "new complete generation wakes the waiting pipe");
    Expect(state.MarkDelivered() == generation, "multi-frame generation delivery");
    Expect(!state.CanWriteNext() && !state.HasUndeliveredPublication(),
           "published generation remains stable until consumed");
  }
  state.ClientClosed();
  Expect(state.CanWriteNext(), "pipe shutdown releases a host writer");
  state.Publish();
  Expect(state.ready() == 6, "optional post-disconnect host frames do not deadlock");
}

void TestLateAndAbsentClients() {
  PublicationState absent;
  absent.Publish();
  absent.Publish();
  Expect(absent.ready() == 2 && absent.CanWriteNext(),
         "DLL without a pipe client can replay multiple frames");

  PublicationState late;
  late.Publish();
  late.Publish();
  late.ClientConnected();
  late.AcknowledgeDelivered();
  Expect(late.MarkDelivered() == 2, "late client receives latest complete generation");
  Expect(!late.CanWriteNext(), "late client's publication is also stable");
  late.ClientClosed();
  Expect(late.CanWriteNext(), "late client close wakes publisher");
}

struct TestCase {
  const char *name;
  void (*run)();
};

}  // namespace

int main() {
  const std::array<TestCase, 6> tests{{
      {"BWAPI burrowed visibility", TestBurrowedVisibility},
      {"burrowed lifecycle transition", TestBurrowedLifecycleTransition},
      {"official SHA-256 vectors", TestOfficialSha256},
      {"binary turn-command framing", TestTurnCommandFraming},
      {"client-driven publication generations", TestPublicationSequence},
      {"late and absent pipe clients", TestLateAndAbsentClients},
  }};
  std::size_t passed = 0;
  for (const TestCase &test : tests) {
    try {
      test.run();
      ++passed;
      std::cout << "PASS: " << test.name << "\n";
    } catch (const std::exception &error) {
      std::cerr << "FAIL: " << test.name << ": " << error.what() << "\n";
      return EXIT_FAILURE;
    }
  }
  std::cout << passed << " deterministic host-logic tests passed\n";
  return EXIT_SUCCESS;
}
