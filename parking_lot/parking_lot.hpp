// Parking lot: multiple levels, spots sized for vehicle types, entry/exit
// tickets and a pluggable fee calculation (Strategy pattern).
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace parking {

using Minutes = std::chrono::minutes;
using Money = std::int64_t;  // in the smallest currency unit (e.g. cents) to avoid floating point

enum class VehicleType { Motorcycle, Car, Truck };
enum class SpotSize { Small, Compact, Large };

inline const char* to_string(VehicleType type) {
    switch (type) {
        case VehicleType::Motorcycle: return "Motorcycle";
        case VehicleType::Car: return "Car";
        case VehicleType::Truck: return "Truck";
    }
    return "?";
}

// A motorcycle fits anywhere, a car needs Compact or Large, a truck needs Large.
inline bool fits(VehicleType vehicle, SpotSize spot) {
    switch (vehicle) {
        case VehicleType::Motorcycle: return true;
        case VehicleType::Car: return spot != SpotSize::Small;
        case VehicleType::Truck: return spot == SpotSize::Large;
    }
    return false;
}

struct Vehicle {
    std::string plate;
    VehicleType type;
};

class ParkingSpot {
public:
    ParkingSpot(std::string id, SpotSize size) : id_(std::move(id)), size_(size) {}

    const std::string& id() const { return id_; }
    SpotSize size() const { return size_; }
    bool is_free() const { return !plate_.has_value(); }
    const std::optional<std::string>& parked_plate() const { return plate_; }

    void occupy(const std::string& plate) {
        if (!is_free()) throw std::logic_error("spot " + id_ + " is already occupied");
        plate_ = plate;
    }
    void release() { plate_.reset(); }

private:
    std::string id_;
    SpotSize size_;
    std::optional<std::string> plate_;
};

class Level {
public:
    // Creates `small` + `compact` + `large` spots named like "L1-C3".
    Level(int number, int small, int compact, int large) : number_(number) {
        add_spots(SpotSize::Small, small, 'S');
        add_spots(SpotSize::Compact, compact, 'C');
        add_spots(SpotSize::Large, large, 'L');
    }

    int number() const { return number_; }

    // Best fit: the smallest free spot the vehicle fits in, so large spots stay
    // available for trucks. Returns nullptr if the level has no suitable spot.
    ParkingSpot* find_spot(VehicleType type) {
        ParkingSpot* best = nullptr;
        for (ParkingSpot& spot : spots_) {
            if (spot.is_free() && fits(type, spot.size()) && (!best || spot.size() < best->size())) {
                best = &spot;
            }
        }
        return best;
    }

    ParkingSpot& spot(const std::string& id) {
        for (ParkingSpot& s : spots_)
            if (s.id() == id) return s;
        throw std::out_of_range("no spot " + id + " on level " + std::to_string(number_));
    }

    int free_spots(SpotSize size) const {
        int count = 0;
        for (const ParkingSpot& s : spots_)
            if (s.size() == size && s.is_free()) ++count;
        return count;
    }

private:
    void add_spots(SpotSize size, int count, char tag) {
        for (int i = 1; i <= count; ++i)
            spots_.emplace_back("L" + std::to_string(number_) + "-" + tag + std::to_string(i), size);
    }

    int number_;
    std::vector<ParkingSpot> spots_;  // never resized after construction, so pointers stay valid
};

struct Ticket {
    std::string id;
    Vehicle vehicle;
    int level;
    std::string spot_id;
    Minutes entry_time;
};

struct Receipt {
    Ticket ticket;
    Minutes exit_time;
    Minutes duration;
    Money fee;
};

// ---- Strategy pattern: how much a stay costs ----------------------------------

class FeeStrategy {
public:
    virtual ~FeeStrategy() = default;
    virtual Money calculate(VehicleType type, Minutes duration) const = 0;
};

// Charges per started hour, with a per-vehicle-type rate and an optional free grace period.
class HourlyFeeStrategy : public FeeStrategy {
public:
    HourlyFeeStrategy(std::map<VehicleType, Money> rate_per_hour, Minutes grace = Minutes(0))
        : rates_(std::move(rate_per_hour)), grace_(grace) {}

    Money calculate(VehicleType type, Minutes duration) const override {
        if (duration <= grace_) return 0;
        const auto hours = (duration.count() + 59) / 60;  // round up to started hours
        return hours * rates_.at(type);
    }

private:
    std::map<VehicleType, Money> rates_;
    Minutes grace_;
};

// One price per visit regardless of duration (e.g. event parking).
class FlatFeeStrategy : public FeeStrategy {
public:
    explicit FlatFeeStrategy(Money fee) : fee_(fee) {}
    Money calculate(VehicleType, Minutes) const override { return fee_; }

private:
    Money fee_;
};

// ---- The lot itself ---------------------------------------------------------

class ParkingLot {
public:
    ParkingLot(std::vector<Level> levels, std::unique_ptr<FeeStrategy> fees)
        : levels_(std::move(levels)), fees_(std::move(fees)) {
        if (!fees_) throw std::invalid_argument("fee strategy is required");
    }

    // Parks the vehicle on the lowest level that has a fitting spot.
    // Returns std::nullopt when the lot is full for this vehicle type.
    std::optional<Ticket> park(const Vehicle& vehicle, Minutes entry_time) {
        if (plate_to_ticket_.count(vehicle.plate))
            throw std::logic_error("vehicle " + vehicle.plate + " is already parked");

        for (Level& level : levels_) {
            if (ParkingSpot* spot = level.find_spot(vehicle.type)) {
                spot->occupy(vehicle.plate);
                Ticket ticket{"T" + std::to_string(++ticket_counter_), vehicle, level.number(), spot->id(),
                              entry_time};
                active_.emplace(ticket.id, ticket);
                plate_to_ticket_.emplace(vehicle.plate, ticket.id);
                return ticket;
            }
        }
        return std::nullopt;
    }

    // Frees the spot and charges according to the current fee strategy.
    Receipt exit(const std::string& ticket_id, Minutes exit_time) {
        auto it = active_.find(ticket_id);
        if (it == active_.end()) throw std::invalid_argument("unknown or already used ticket " + ticket_id);
        const Ticket ticket = it->second;
        if (exit_time < ticket.entry_time) throw std::invalid_argument("exit time is before entry time");

        level(ticket.level).spot(ticket.spot_id).release();
        active_.erase(it);
        plate_to_ticket_.erase(ticket.vehicle.plate);

        const Minutes duration = exit_time - ticket.entry_time;
        return Receipt{ticket, exit_time, duration, fees_->calculate(ticket.vehicle.type, duration)};
    }

    // Swap pricing at runtime (e.g. weekend rates) without touching the lot logic.
    void set_fee_strategy(std::unique_ptr<FeeStrategy> fees) {
        if (!fees) throw std::invalid_argument("fee strategy is required");
        fees_ = std::move(fees);
    }

    int free_spots(SpotSize size) const {
        int count = 0;
        for (const Level& l : levels_) count += l.free_spots(size);
        return count;
    }

    std::size_t parked_count() const { return active_.size(); }
    bool is_parked(const std::string& plate) const { return plate_to_ticket_.count(plate) != 0; }

private:
    Level& level(int number) {
        for (Level& l : levels_)
            if (l.number() == number) return l;
        throw std::out_of_range("no level " + std::to_string(number));
    }

    std::vector<Level> levels_;
    std::unique_ptr<FeeStrategy> fees_;
    std::unordered_map<std::string, Ticket> active_;               // ticket id -> ticket
    std::unordered_map<std::string, std::string> plate_to_ticket_;  // plate -> ticket id
    std::uint64_t ticket_counter_ = 0;
};

}  // namespace parking
