#include "tracker.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lifecycle = pluto_bridge::lifecycle;
using lifecycle::Event;
using lifecycle::EventType;
using lifecycle::Tracker;
using lifecycle::UnitInput;
using lifecycle::UpdateReport;

namespace {

struct Result {
  UpdateReport report;
  std::vector<Event> events;
};

UnitInput Unit(std::uint32_t slot = 7, std::uint8_t generation = 1) {
  UnitInput input;
  input.slot = slot;
  input.present = true;
  input.generation = generation;
  input.visible = true;
  input.detected = true;
  input.completed = true;
  input.type = 37;
  input.player = 0;
  return input;
}

Result Update(Tracker &tracker, const std::vector<UnitInput> &inputs,
              bool initial = false, std::size_t capacity = 128) {
  std::array<Event, 128> storage{};
  Result result;
  result.report = tracker.Update(inputs.data(), inputs.size(), initial,
                                 storage.data(), capacity);
  result.events.assign(storage.begin(), storage.begin() + result.report.emitted);
  return result;
}

const char *Name(EventType type) {
  switch (type) {
    case EventType::UnitCreate: return "Create";
    case EventType::UnitDiscover: return "Discover";
    case EventType::UnitShow: return "Show";
    case EventType::UnitHide: return "Hide";
    case EventType::UnitEvade: return "Evade";
    case EventType::UnitDestroy: return "Destroy";
    case EventType::UnitMorph: return "Morph";
    case EventType::UnitComplete: return "Complete";
    case EventType::UnitRenegade: return "Renegade";
  }
  return "Unknown";
}

std::string Format(const std::vector<Event> &events) {
  std::ostringstream out;
  out << "[";
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (i != 0)
      out << ", ";
    out << Name(events[i].type) << "(" << events[i].unit_id << ")";
  }
  out << "]";
  return out.str();
}

void ExpectEvents(const Result &result,
                  std::initializer_list<Event> expected) {
  const std::vector<Event> wanted(expected);
  if (result.events == wanted)
    return;
  throw std::runtime_error("expected " + Format(wanted) + ", got " +
                           Format(result.events));
}

void Expect(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void TestFrameZeroAndCreateDiscover() {
  Tracker initial;
  Result result = Update(initial, {Unit()}, true);
  ExpectEvents(result, {{EventType::UnitDiscover, 0}});
  Expect(initial.PackedId(7) == 0, "initial packed id");

  Tracker initial_order;
  UnitInput first = Unit(9, 1);
  UnitInput second = Unit(2, 1);
  result = Update(initial_order, {first, second}, true);
  ExpectEvents(result, {{EventType::UnitDiscover, 0},
                        {EventType::UnitDiscover, 1}});

  Tracker tracker;
  Update(tracker, {}, true);
  UnitInput incomplete = Unit();
  incomplete.completed = false;
  result = Update(tracker, {incomplete});
  ExpectEvents(result, {{EventType::UnitCreate, 0},
                        {EventType::UnitDiscover, 0},
                        {EventType::UnitShow, 0}});
  Expect(Update(tracker, {incomplete}).events.empty(), "steady create state");

  Tracker already_complete;
  Update(already_complete, {}, true);
  result = Update(already_complete, {Unit()});
  ExpectEvents(result, {{EventType::UnitCreate, 0},
                        {EventType::UnitComplete, 0},
                        {EventType::UnitDiscover, 0},
                        {EventType::UnitShow, 0}});

  Tracker initial_neutral;
  UnitInput neutral = Unit(20, 1);
  neutral.player = 11;
  neutral.visible = false;
  neutral.detected = false;
  result = Update(initial_neutral, {neutral}, true);
  ExpectEvents(result, {{EventType::UnitDiscover, 0}});

  Tracker hidden_initial;
  UnitInput hidden = Unit(21, 1);
  hidden.player = 1;
  hidden.visible = false;
  hidden.detected = false;
  Expect(Update(hidden_initial, {hidden}, true).events.empty(),
         "hidden initial enemy has no discovery");
  result = Update(hidden_initial, {Unit(21, 1)});
  // wasAlive was baselined, but wasCompleted was not: genuine later
  // accessibility produces Complete/Discover/Show without a second Create.
  ExpectEvents(result, {{EventType::UnitComplete, 0},
                        {EventType::UnitDiscover, 0},
                        {EventType::UnitShow, 0}});
  Expect(Update(hidden_initial, {Unit(21, 1)}).events.empty(),
         "initially hidden completion is emitted once");
}

void TestFogEvadeThenRediscover() {
  Tracker tracker;
  Update(tracker, {Unit()}, true);
  UnitInput hidden = Unit();
  hidden.visible = false;
  hidden.detected = false;
  Result result = Update(tracker, {hidden});
  ExpectEvents(result, {{EventType::UnitHide, 0}, {EventType::UnitEvade, 0}});
  Expect(Update(tracker, {hidden}).events.empty(), "steady fog state");
  result = Update(tracker, {Unit()});
  ExpectEvents(result, {{EventType::UnitDiscover, 0}, {EventType::UnitShow, 0}});

  // BWAPI's canAccess/isVisible lifecycle does not equate detection with
  // visibility: a visible, undetected cloaked unit can still Discover/Show.
  UnitInput undetected = hidden;
  undetected.visible = true;
  result = Update(tracker, {undetected});
  Expect(result.events.empty(), "steady visible state with detection off");
  Expect(!tracker.Detected(7), "detected state is transported independently");
}

void TestDeath() {
  Tracker tracker;
  Update(tracker, {Unit()}, true);
  Result result = Update(tracker, {});
  ExpectEvents(result, {{EventType::UnitHide, 0},
                        {EventType::UnitEvade, 0},
                        {EventType::UnitDestroy, 0}});
  Expect(Update(tracker, {}).events.empty(), "death is emitted once");
  Expect(tracker.PackedId(7) == -1, "dead slot has no current packed id");
}

void TestInvisibleDeathAfterEvade() {
  Tracker tracker;
  Update(tracker, {Unit()}, true);
  UnitInput hidden = Unit();
  hidden.visible = false;
  hidden.detected = false;
  Update(tracker, {hidden});
  // GameUnits.cpp's dyingUnits loop only handles wasAccessible units.  Once
  // fog has evaded the unit, its later invisible death adds no duplicate events.
  Expect(Update(tracker, {}).events.empty(), "invisible death after evade");
}

void TestSlotReuseUsesANewIdentity() {
  Tracker tracker;
  UnitInput old = Unit(4, 1);
  Update(tracker, {old}, true);
  UnitInput replacement = Unit(4, 2);
  Result result = Update(tracker, {replacement});
  ExpectEvents(result, {{EventType::UnitHide, 0},
                        {EventType::UnitEvade, 0},
                        {EventType::UnitDestroy, 0},
                        {EventType::UnitCreate, 1},
                        {EventType::UnitComplete, 1},
                        {EventType::UnitDiscover, 1},
                        {EventType::UnitShow, 1}});
  Expect(tracker.PackedId(4) == 1, "replacement packed id");

  UnitInput hidden = replacement;
  hidden.visible = false;
  hidden.detected = false;
  Update(tracker, {hidden});
  replacement.generation = 3;
  result = Update(tracker, {replacement});
  ExpectEvents(result, {{EventType::UnitCreate, 2},
                        {EventType::UnitComplete, 2},
                        {EventType::UnitDiscover, 2},
                        {EventType::UnitShow, 2}});
}

void TestMorph() {
  Tracker tracker;
  Update(tracker, {Unit()}, true);
  UnitInput morphed = Unit();
  morphed.type = 41;
  Result result = Update(tracker, {morphed});
  ExpectEvents(result, {{EventType::UnitMorph, 0}});
  // This is GameUnits.cpp's wasCompleted reset, not a create-time completion.
  ExpectEvents(Update(tracker, {morphed}), {{EventType::UnitComplete, 0}});
  Expect(Update(tracker, {morphed}).events.empty(), "morph completion is once");
}

void TestCompletion() {
  Tracker tracker;
  UnitInput building = Unit();
  building.completed = false;
  Update(tracker, {building}, true);
  building.completed = true;
  ExpectEvents(Update(tracker, {building}), {{EventType::UnitComplete, 0}});
  Expect(Update(tracker, {building}).events.empty(), "completion is emitted once");
}

void TestRenegade() {
  Tracker tracker;
  Update(tracker, {Unit()}, true);
  UnitInput captured = Unit();
  captured.player = 1;
  ExpectEvents(Update(tracker, {captured}), {{EventType::UnitRenegade, 0}});
  Expect(Update(tracker, {captured}).events.empty(), "renegade is emitted once");
}

void TestLegacyGenerationZero() {
  Tracker tracker;
  UnitInput legacy = Unit(9, 0);
  ExpectEvents(Update(tracker, {legacy}, true), {{EventType::UnitDiscover, 0}});
  Expect(Update(tracker, {legacy}).events.empty(), "legacy steady state");
  ExpectEvents(Update(tracker, {}), {{EventType::UnitHide, 0},
                                     {EventType::UnitEvade, 0},
                                     {EventType::UnitDestroy, 0}});
  // Zero is a valid legacy generation, not a sentinel that prevents a fresh
  // packed identity after an observed death.
  ExpectEvents(Update(tracker, {legacy}), {{EventType::UnitCreate, 1},
                                           {EventType::UnitComplete, 1},
                                           {EventType::UnitDiscover, 1},
                                           {EventType::UnitShow, 1}});
}

void TestEventCapacityAndBounds() {
  Tracker tracker;
  Update(tracker, {}, true);
  Result result = Update(tracker, {Unit(1, 1), Unit(2, 1)}, false, 4);
  ExpectEvents(result, {{EventType::UnitCreate, 0},
                        {EventType::UnitComplete, 0},
                        {EventType::UnitDiscover, 0},
                        {EventType::UnitShow, 0}});
  Expect(result.report.emitted == 4 && result.report.dropped == 4,
         "event cap accounting");
  Expect(Update(tracker, {Unit(1, 1), Unit(2, 1)}).events.empty(),
         "dropped events are not repeated");

  Tracker zero_cap;
  UnitInput input = Unit();
  const UpdateReport zero = zero_cap.Update(&input, 1, false, nullptr, 0);
  Expect(zero.emitted == 0 && zero.dropped == 4, "zero-cap destination");

  Tracker bounded;
  UnitInput invalid = Unit(lifecycle::kMaxSlots, 1);
  UnitInput duplicate = Unit(3, 1);
  std::array<UnitInput, 3> inputs{{invalid, duplicate, duplicate}};
  std::array<Event, 8> events{};
  const UpdateReport bounds = bounded.Update(inputs.data(), inputs.size(), true,
                                             events.data(), events.size());
  Expect(bounds.invalid_inputs == 2, "invalid and duplicate input accounting");
  Expect(bounds.emitted == 1 && events[0].unit_id == 0, "valid bounded input");

  Tracker packed_limit(1);
  Update(packed_limit, {}, true);
  result = Update(packed_limit, {Unit(1, 1), Unit(2, 1)});
  Expect(result.report.packed_id_overflow == 1, "packed id overflow accounting");
  Expect(result.events.size() == 4, "no events with invalid packed ids");
  for (const Event &event : result.events)
    Expect(event.unit_id == 0, "only the allocated packed id is emitted");
  Expect(packed_limit.PackedId(2) == -1, "overflowed unit has no packed id");
}

struct TestCase {
  const char *name;
  void (*run)();
};

}  // namespace

int main() {
  const std::array<TestCase, 10> tests{{
      {"frame zero and create/discover", TestFrameZeroAndCreateDiscover},
      {"fog evade then rediscover", TestFogEvadeThenRediscover},
      {"death", TestDeath},
      {"invisible death after evade", TestInvisibleDeathAfterEvade},
      {"slot reuse", TestSlotReuseUsesANewIdentity},
      {"morph", TestMorph},
      {"completion", TestCompletion},
      {"renegade", TestRenegade},
      {"legacy generation zero", TestLegacyGenerationZero},
      {"event capacity and bounds", TestEventCapacityAndBounds},
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
  std::cout << passed << " deterministic lifecycle tests passed\n";
  return EXIT_SUCCESS;
}
