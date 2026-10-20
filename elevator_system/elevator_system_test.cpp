#include "elevator_system.hpp"

#include <vector>

#include "../testing/mini_test.hpp"

using namespace elevator;

TEST(idle_system_stays_idle) {
    ElevatorSystem sys(2, 0, 9);
    CHECK(sys.is_idle());
    sys.step();
    CHECK_EQ(sys.car(0).floor(), 0);
    CHECK(sys.car(0).state() == CarState::Idle);
}

TEST(state_machine_transitions_for_one_trip) {
    ElevatorSystem sys(1, 0, 9);
    sys.request_destination(0, 2);
    std::vector<CarState> states;
    for (int i = 0; i < 4; ++i) {
        sys.step();
        states.push_back(sys.car(0).state());
    }
    // Move 0->1, move 1->2, open doors at 2, close doors.
    CHECK((states == std::vector<CarState>{CarState::MovingUp, CarState::MovingUp, CarState::DoorsOpen, CarState::Idle}));
    CHECK_EQ(sys.car(0).floor(), 2);
    CHECK(sys.is_idle());
}

TEST(scan_serves_stops_in_sweep_order_not_request_order) {
    ElevatorSystem sys(1, 0, 9);
    sys.request_destination(0, 8);
    sys.request_destination(0, 2);
    sys.request_destination(0, 5);
    sys.run_until_idle();
    CHECK((sys.car(0).stops_served() == std::vector<int>{2, 5, 8}));
}

TEST(scan_finishes_sweep_before_reversing) {
    ElevatorSystem sys(1, 0, 9);
    sys.request_destination(0, 6);
    for (int i = 0; i < 3; ++i) sys.step();  // now at floor 3 heading up
    CHECK_EQ(sys.car(0).floor(), 3);
    sys.request_destination(0, 1);  // behind us
    sys.request_destination(0, 4);  // ahead of us
    sys.run_until_idle();
    CHECK((sys.car(0).stops_served() == std::vector<int>{4, 6, 1}));
}

TEST(up_sweep_skips_down_calls_until_the_top) {
    ElevatorSystem sys(1, 0, 9);
    sys.request_pickup(3, Direction::Down);  // wants to go down: pick up on the way back
    sys.request_destination(0, 7);
    sys.run_until_idle();
    CHECK((sys.car(0).stops_served() == std::vector<int>{7, 3}));
}

TEST(down_call_at_top_of_sweep_reverses_direction) {
    ElevatorSystem sys(1, 0, 9);
    sys.request_pickup(5, Direction::Down);
    sys.step();  // dispatch + start moving up
    CHECK(sys.car(0).direction() == Direction::Up);
    while (sys.car(0).state() != CarState::DoorsOpen) sys.step();
    CHECK_EQ(sys.car(0).floor(), 5);
    CHECK(sys.car(0).direction() == Direction::Down);
    sys.request_destination(0, 1);
    sys.run_until_idle();
    CHECK((sys.car(0).stops_served() == std::vector<int>{5, 1}));
}

TEST(pickup_at_current_floor_opens_doors_immediately) {
    ElevatorSystem sys(1, 0, 9);
    sys.request_pickup(0, Direction::Up);
    sys.step();
    CHECK(sys.car(0).state() == CarState::DoorsOpen);
    CHECK_EQ(sys.car(0).floor(), 0);
}

TEST(dispatcher_picks_nearest_idle_car) {
    ElevatorSystem sys(2, 0, 9);
    sys.request_destination(1, 8);
    sys.run_until_idle();  // car 1 parks at floor 8, car 0 stays at 0
    sys.request_pickup(7, Direction::Down);
    sys.step();
    CHECK(sys.car(1).has_call({7, Direction::Down}));
    CHECK(!sys.car(0).has_call({7, Direction::Down}));
}

TEST(dispatcher_prefers_car_already_heading_that_way) {
    ElevatorSystem sys(2, 0, 9);
    sys.request_destination(0, 9);
    sys.step();
    sys.step();  // car 0 at floor 2 heading up; car 1 idle at floor 0
    sys.request_pickup(5, Direction::Up);
    sys.step();
    CHECK(sys.car(0).has_call({5, Direction::Up}));
    // A down call below car 0 should go to the idle car instead.
    sys.request_pickup(1, Direction::Down);
    sys.step();
    CHECK(sys.car(1).has_call({1, Direction::Down}));
}

TEST(duplicate_hall_calls_are_merged) {
    ElevatorSystem sys(2, 0, 9);
    sys.request_pickup(4, Direction::Up);
    sys.request_pickup(4, Direction::Up);
    sys.step();
    CHECK_EQ(sys.car(0).pending_stops() + sys.car(1).pending_stops(), 1u);
}

TEST(invalid_requests_are_rejected) {
    ElevatorSystem sys(1, 0, 9);
    CHECK_THROWS(sys.request_destination(0, 10), std::out_of_range);
    CHECK_THROWS(sys.request_pickup(-1, Direction::Up), std::out_of_range);
    CHECK_THROWS(sys.request_pickup(9, Direction::Up), std::invalid_argument);
    CHECK_THROWS(sys.request_pickup(0, Direction::Down), std::invalid_argument);
    CHECK_THROWS(sys.request_pickup(3, Direction::Idle), std::invalid_argument);
    CHECK_THROWS(ElevatorSystem(0, 0, 9), std::invalid_argument);
}

TEST(busy_building_simulation_serves_everyone) {
    // Deterministic pseudo-random traffic: every call and destination must be served.
    ElevatorSystem sys(3, 0, 19);
    unsigned seed = 7;
    auto rnd = [&seed](int n) { seed = seed * 1103515245u + 12345u; return static_cast<int>((seed >> 16) % n); };
    for (int t = 0; t < 300; ++t) {
        if (t % 3 == 0) {
            int floor = rnd(20);
            Direction d = floor == 19 ? Direction::Down : (floor == 0 || rnd(2) ? Direction::Up : Direction::Down);
            sys.request_pickup(floor, d);
        }
        if (t % 5 == 0) sys.request_destination(rnd(3), rnd(20));
        sys.step();
    }
    int ticks = sys.run_until_idle(1000);
    CHECK(ticks < 200);
    CHECK(sys.is_idle());
    for (std::size_t i = 0; i < sys.car_count(); ++i) CHECK(!sys.car(static_cast<int>(i)).has_work());
}

MINI_TEST_MAIN()
