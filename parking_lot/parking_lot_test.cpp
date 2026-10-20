#include "parking_lot.hpp"

#include <memory>
#include <vector>

#include "../testing/mini_test.hpp"

using namespace parking;
using std::chrono::hours;

namespace {

std::unique_ptr<FeeStrategy> standard_rates() {
    return std::make_unique<HourlyFeeStrategy>(
        std::map<VehicleType, Money>{{VehicleType::Motorcycle, 100}, {VehicleType::Car, 200}, {VehicleType::Truck, 500}},
        Minutes(15));
}

// Level 1: 1 small, 1 compact, 1 large. Level 2: 0 small, 1 compact, 1 large.
ParkingLot make_lot() {
    std::vector<Level> levels;
    levels.emplace_back(1, 1, 1, 1);
    levels.emplace_back(2, 0, 1, 1);
    return ParkingLot(std::move(levels), standard_rates());
}

}  // namespace

TEST(vehicle_spot_compatibility) {
    CHECK(fits(VehicleType::Motorcycle, SpotSize::Small));
    CHECK(fits(VehicleType::Motorcycle, SpotSize::Large));
    CHECK(!fits(VehicleType::Car, SpotSize::Small));
    CHECK(fits(VehicleType::Car, SpotSize::Compact));
    CHECK(fits(VehicleType::Car, SpotSize::Large));
    CHECK(!fits(VehicleType::Truck, SpotSize::Compact));
    CHECK(fits(VehicleType::Truck, SpotSize::Large));
}

TEST(park_uses_smallest_fitting_spot_on_lowest_level) {
    ParkingLot lot = make_lot();
    auto bike = lot.park({"BIKE-1", VehicleType::Motorcycle}, Minutes(0));
    REQUIRE(bike.has_value());
    CHECK_EQ(bike->level, 1);
    CHECK_EQ(bike->spot_id, std::string("L1-S1"));

    auto car = lot.park({"CAR-1", VehicleType::Car}, Minutes(0));
    REQUIRE(car.has_value());
    CHECK_EQ(car->spot_id, std::string("L1-C1"));  // compact, not large

    auto car2 = lot.park({"CAR-2", VehicleType::Car}, Minutes(0));
    REQUIRE(car2.has_value());
    CHECK_EQ(car2->spot_id, std::string("L1-L1"));  // level 1 still beats level 2
}

TEST(overflow_to_next_level_then_full) {
    ParkingLot lot = make_lot();
    auto t1 = lot.park({"TRUCK-1", VehicleType::Truck}, Minutes(0));
    auto t2 = lot.park({"TRUCK-2", VehicleType::Truck}, Minutes(0));
    REQUIRE(t1.has_value());
    REQUIRE(t2.has_value());
    CHECK_EQ(t1->level, 1);
    CHECK_EQ(t2->level, 2);
    CHECK(!lot.park({"TRUCK-3", VehicleType::Truck}, Minutes(0)).has_value());
    CHECK_EQ(lot.free_spots(SpotSize::Large), 0);
    // Cars still fit in compact spots.
    CHECK(lot.park({"CAR-1", VehicleType::Car}, Minutes(0)).has_value());
}

TEST(exit_frees_spot_and_charges_started_hours) {
    ParkingLot lot = make_lot();
    auto ticket = lot.park({"CAR-1", VehicleType::Car}, Minutes(60));
    REQUIRE(ticket.has_value());
    CHECK_EQ(lot.free_spots(SpotSize::Compact), 1);

    Receipt r = lot.exit(ticket->id, Minutes(60 + 61));  // 61 minutes -> 2 started hours
    CHECK_EQ(r.duration.count(), 61);
    CHECK_EQ(r.fee, 400);
    CHECK_EQ(lot.free_spots(SpotSize::Compact), 2);
    CHECK(!lot.is_parked("CAR-1"));
    CHECK_EQ(lot.parked_count(), 0u);
}

TEST(grace_period_is_free) {
    ParkingLot lot = make_lot();
    auto ticket = lot.park({"CAR-1", VehicleType::Car}, Minutes(0));
    CHECK_EQ(lot.exit(ticket->id, Minutes(15)).fee, 0);
}

TEST(truck_rate_differs_from_car_rate) {
    ParkingLot lot = make_lot();
    auto truck = lot.park({"TRUCK-1", VehicleType::Truck}, Minutes(0));
    CHECK_EQ(lot.exit(truck->id, hours(3)).fee, 1500);
}

TEST(fee_strategy_can_be_swapped_at_runtime) {
    ParkingLot lot = make_lot();
    auto ticket = lot.park({"CAR-1", VehicleType::Car}, Minutes(0));
    lot.set_fee_strategy(std::make_unique<FlatFeeStrategy>(999));
    CHECK_EQ(lot.exit(ticket->id, hours(10)).fee, 999);
}

TEST(ticket_cannot_be_used_twice) {
    ParkingLot lot = make_lot();
    auto ticket = lot.park({"CAR-1", VehicleType::Car}, Minutes(0));
    lot.exit(ticket->id, Minutes(30));
    CHECK_THROWS(lot.exit(ticket->id, Minutes(40)), std::invalid_argument);
    CHECK_THROWS(lot.exit("T-does-not-exist", Minutes(40)), std::invalid_argument);
}

TEST(same_vehicle_cannot_park_twice) {
    ParkingLot lot = make_lot();
    lot.park({"CAR-1", VehicleType::Car}, Minutes(0));
    CHECK_THROWS(lot.park({"CAR-1", VehicleType::Car}, Minutes(5)), std::logic_error);
}

TEST(exit_before_entry_is_rejected) {
    ParkingLot lot = make_lot();
    auto ticket = lot.park({"CAR-1", VehicleType::Car}, Minutes(100));
    CHECK_THROWS(lot.exit(ticket->id, Minutes(50)), std::invalid_argument);
    CHECK(lot.is_parked("CAR-1"));  // failed exit leaves state unchanged
}

TEST(freed_spot_is_reused) {
    ParkingLot lot = make_lot();
    auto t1 = lot.park({"TRUCK-1", VehicleType::Truck}, Minutes(0));
    lot.park({"TRUCK-2", VehicleType::Truck}, Minutes(0));
    lot.exit(t1->id, Minutes(10));
    auto t3 = lot.park({"TRUCK-3", VehicleType::Truck}, Minutes(20));
    REQUIRE(t3.has_value());
    CHECK_EQ(t3->spot_id, std::string("L1-L1"));
    CHECK(t3->id != t1->id);  // ticket ids are never reused
}

MINI_TEST_MAIN()
