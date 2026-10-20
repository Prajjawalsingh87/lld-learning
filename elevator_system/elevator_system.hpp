// Elevator system: hall calls go into a request queue, a dispatcher assigns
// each call to the cheapest car, and every car runs a small state machine with
// SCAN ("elevator algorithm") scheduling: keep moving in the current direction
// while there is work ahead, then reverse.
//
// Time is discrete: ElevatorSystem::step() advances the simulation by one tick.
// In one tick a car does exactly one thing: move one floor, open its doors,
// close its doors, or stay idle.
#pragma once

#include <cstdlib>
#include <deque>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace elevator {

enum class Direction { Up, Down, Idle };
enum class CarState { Idle, MovingUp, MovingDown, DoorsOpen };

inline const char* to_string(CarState s) {
    switch (s) {
        case CarState::Idle: return "Idle";
        case CarState::MovingUp: return "MovingUp";
        case CarState::MovingDown: return "MovingDown";
        case CarState::DoorsOpen: return "DoorsOpen";
    }
    return "?";
}

// A button pressed on a floor: "I am at `floor` and want to go `direction`".
struct HallCall {
    int floor;
    Direction direction;  // Up or Down
};

class ElevatorCar {
public:
    ElevatorCar(int id, int start_floor) : id_(id), floor_(start_floor) {}

    int id() const { return id_; }
    int floor() const { return floor_; }
    CarState state() const { return state_; }
    Direction direction() const { return direction_; }
    // Floors where this car opened its doors, in order. Handy for tests and demos.
    const std::vector<int>& stops_served() const { return served_; }

    // Button pressed inside the car.
    void add_destination(int floor) { car_stops_.insert(floor); }

    // Hall call assigned to this car by the dispatcher.
    void assign(const HallCall& call) {
        (call.direction == Direction::Up ? up_calls_ : down_calls_).insert(call.floor);
    }

    bool has_call(const HallCall& call) const {
        return (call.direction == Direction::Up ? up_calls_ : down_calls_).count(call.floor) != 0;
    }

    bool has_work() const { return !car_stops_.empty() || !up_calls_.empty() || !down_calls_.empty(); }
    std::size_t pending_stops() const { return car_stops_.size() + up_calls_.size() + down_calls_.size(); }

    // ---- State machine, one tick ----
    //   DoorsOpen          -> Idle (doors close)
    //   any, stop here     -> DoorsOpen
    //   work above / below -> MovingUp / MovingDown (one floor)
    //   nothing to do      -> Idle
    void step() {
        if (state_ == CarState::DoorsOpen) {
            state_ = CarState::Idle;
            return;
        }
        if (should_stop_here()) {
            serve_current_floor();
            state_ = CarState::DoorsOpen;
            return;
        }
        update_direction();
        if (direction_ == Direction::Up) {
            ++floor_;
            state_ = CarState::MovingUp;
        } else if (direction_ == Direction::Down) {
            --floor_;
            state_ = CarState::MovingDown;
        } else {
            state_ = CarState::Idle;
        }
    }

    // Dispatcher cost: roughly how many floors until this car can pick up `call`.
    // Cheap if the car is idle or already heading towards the call in the same
    // direction; expensive if it must finish its sweep and come back.
    int cost(const HallCall& call, int floors_in_building) const {
        const int distance = std::abs(call.floor - floor_);
        if (!has_work()) return distance;
        const bool on_the_way = (direction_ == Direction::Up && call.direction == Direction::Up && call.floor >= floor_) ||
                                (direction_ == Direction::Down && call.direction == Direction::Down && call.floor <= floor_);
        if (on_the_way) return distance;
        return distance + 2 * floors_in_building;
    }

private:
    bool any_above() const { return any_beyond(floor_, true); }
    bool any_below() const { return any_beyond(floor_, false); }

    bool any_beyond(int floor, bool above) const {
        for (const std::set<int>* s : {&car_stops_, &up_calls_, &down_calls_}) {
            if (s->empty()) continue;
            if (above && *s->rbegin() > floor) return true;
            if (!above && *s->begin() < floor) return true;
        }
        return false;
    }

    // SCAN rule: on the way up only stop for car stops and "up" hall calls; a
    // "down" call is picked up only at the top of the sweep (nothing above).
    bool should_stop_here() const {
        const int f = floor_;
        if (car_stops_.count(f)) return true;
        switch (direction_) {
            case Direction::Up: return up_calls_.count(f) || (!any_above() && down_calls_.count(f));
            case Direction::Down: return down_calls_.count(f) || (!any_below() && up_calls_.count(f));
            case Direction::Idle: return up_calls_.count(f) || down_calls_.count(f);
        }
        return false;
    }

    void serve_current_floor() {
        const int f = floor_;
        car_stops_.erase(f);
        if (direction_ == Direction::Up) {
            if (!up_calls_.erase(f) && !any_above() && down_calls_.erase(f)) direction_ = Direction::Down;
        } else if (direction_ == Direction::Down) {
            if (!down_calls_.erase(f) && !any_below() && up_calls_.erase(f)) direction_ = Direction::Up;
        } else if (up_calls_.erase(f)) {
            direction_ = Direction::Up;  // passengers want to go up
        } else if (down_calls_.erase(f)) {
            direction_ = Direction::Down;
        }
        served_.push_back(f);
    }

    void update_direction() {
        const bool up = any_above(), down = any_below();
        if (direction_ == Direction::Up && !up) direction_ = down ? Direction::Down : Direction::Idle;
        else if (direction_ == Direction::Down && !down) direction_ = up ? Direction::Up : Direction::Idle;
        else if (direction_ == Direction::Idle) direction_ = up ? Direction::Up : (down ? Direction::Down : Direction::Idle);
    }

    int id_;
    int floor_;
    CarState state_ = CarState::Idle;
    Direction direction_ = Direction::Idle;  // current SCAN sweep direction
    std::set<int> car_stops_;                // destinations pressed inside the car
    std::set<int> up_calls_;                 // assigned hall calls going up
    std::set<int> down_calls_;               // assigned hall calls going down
    std::vector<int> served_;
};

class ElevatorSystem {
public:
    ElevatorSystem(int car_count, int lowest_floor, int highest_floor)
        : lowest_(lowest_floor), highest_(highest_floor) {
        if (car_count <= 0) throw std::invalid_argument("need at least one car");
        if (lowest_floor >= highest_floor) throw std::invalid_argument("need at least two floors");
        for (int i = 0; i < car_count; ++i) cars_.emplace_back(i, lowest_floor);
    }

    // Hall button. Queued now, assigned to a car on the next step().
    void request_pickup(int floor, Direction direction) {
        validate_floor(floor);
        if (direction == Direction::Idle) throw std::invalid_argument("hall call needs a direction");
        if ((floor == highest_ && direction == Direction::Up) || (floor == lowest_ && direction == Direction::Down))
            throw std::invalid_argument("no floor in that direction");
        pending_.push_back({floor, direction});
    }

    // Button inside a car.
    void request_destination(int car_id, int floor) {
        validate_floor(floor);
        mutable_car(car_id).add_destination(floor);
    }

    // Advance the simulation by one tick: dispatch queued calls, then move every car.
    void step() {
        dispatch();
        for (ElevatorCar& c : cars_) c.step();
        ++tick_;
    }

    // Runs until every request is served and all doors are closed. Returns ticks used.
    int run_until_idle(int max_ticks = 10000) {
        const long start = tick_;
        while (!is_idle()) {
            if (tick_ - start >= max_ticks) throw std::runtime_error("simulation did not settle");
            step();
        }
        return static_cast<int>(tick_ - start);
    }

    bool is_idle() const {
        if (!pending_.empty()) return false;
        for (const ElevatorCar& c : cars_)
            if (c.has_work() || c.state() == CarState::DoorsOpen) return false;
        return true;
    }

    const ElevatorCar& car(int id) const { return cars_.at(static_cast<std::size_t>(id)); }
    std::size_t car_count() const { return cars_.size(); }
    long tick() const { return tick_; }
    std::size_t pending_requests() const { return pending_.size(); }

private:
    ElevatorCar& mutable_car(int id) { return cars_.at(static_cast<std::size_t>(id)); }

    void validate_floor(int floor) const {
        if (floor < lowest_ || floor > highest_) throw std::out_of_range("floor " + std::to_string(floor) + " does not exist");
    }

    void dispatch() {
        const int floors = highest_ - lowest_ + 1;
        while (!pending_.empty()) {
            const HallCall call = pending_.front();
            pending_.pop_front();

            bool already_assigned = false;
            for (const ElevatorCar& c : cars_) already_assigned |= c.has_call(call);
            if (already_assigned) continue;  // someone pressed the same button twice

            ElevatorCar* best = &cars_.front();
            for (ElevatorCar& c : cars_)
                if (c.cost(call, floors) < best->cost(call, floors)) best = &c;  // ties: lowest id
            best->assign(call);
        }
    }

    int lowest_;
    int highest_;
    long tick_ = 0;
    std::vector<ElevatorCar> cars_;
    std::deque<HallCall> pending_;  // request queue (FIFO)
};

}  // namespace elevator
