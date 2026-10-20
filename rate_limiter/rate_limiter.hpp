// Rate limiters behind one interface:
//   - TokenBucketLimiter: allows bursts up to `capacity`, refills at a steady rate.
//   - SlidingWindowLogLimiter: at most `limit` requests in any rolling `window`.
// Both keep separate state per client key, are thread-safe (std::mutex) and
// read time from an injected Clock so tests can control time exactly.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ratelimit {

using Nanos = std::chrono::nanoseconds;

// ---- Clock abstraction (dependency injection for deterministic tests) -------

class Clock {
public:
    virtual ~Clock() = default;
    virtual Nanos now() const = 0;  // monotonic time since an arbitrary epoch
};

class SteadyClock : public Clock {
public:
    Nanos now() const override {
        return std::chrono::duration_cast<Nanos>(std::chrono::steady_clock::now().time_since_epoch());
    }
};

// Time only moves when the test says so. Safe to read from many threads.
class ManualClock : public Clock {
public:
    explicit ManualClock(Nanos start = Nanos(0)) : now_(start.count()) {}
    Nanos now() const override { return Nanos(now_.load()); }
    void advance(Nanos d) { now_ += d.count(); }

private:
    std::atomic<std::int64_t> now_;
};

// ---- Common interface -------------------------------------------------------

class RateLimiter {
public:
    virtual ~RateLimiter() = default;
    // Returns true if the request from `client` may proceed (and records it).
    virtual bool allow(const std::string& client) = 0;
};

// ---- Token bucket -----------------------------------------------------------
//
// Each client has a bucket holding up to `capacity` tokens; a request costs one
// token; tokens are added continuously at `refill_tokens` per `refill_period`.
// To stay exact (no floating point drift), the bucket stores "token-nanoseconds":
// one token == refill_period_ns units of credit, and every elapsed nanosecond adds
// refill_tokens units.
class TokenBucketLimiter : public RateLimiter {
public:
    TokenBucketLimiter(std::int64_t capacity, std::int64_t refill_tokens, Nanos refill_period,
                       std::shared_ptr<const Clock> clock)
        : capacity_(capacity),
          refill_tokens_(refill_tokens),
          period_ns_(refill_period.count()),
          clock_(std::move(clock)) {
        if (capacity_ <= 0 || refill_tokens_ <= 0 || period_ns_ <= 0)
            throw std::invalid_argument("token bucket parameters must be positive");
        if (!clock_) throw std::invalid_argument("clock is required");
        if (capacity_ > INT64_MAX / period_ns_) throw std::invalid_argument("capacity * period overflows");
    }

    bool allow(const std::string& client) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::int64_t now = clock_->now().count();
        const std::int64_t max_credit = capacity_ * period_ns_;

        auto [it, inserted] = buckets_.try_emplace(client, Bucket{max_credit, now});  // new clients start full
        Bucket& b = it->second;
        if (!inserted) refill(b, now, max_credit);

        if (b.credit < period_ns_) return false;
        b.credit -= period_ns_;
        return true;
    }

    // Whole tokens currently available for a client (refills first). For tests/monitoring.
    std::int64_t available(const std::string& client) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::int64_t max_credit = capacity_ * period_ns_;
        auto it = buckets_.find(client);
        if (it == buckets_.end()) return capacity_;
        refill(it->second, clock_->now().count(), max_credit);
        return it->second.credit / period_ns_;
    }

private:
    struct Bucket {
        std::int64_t credit;  // in token-nanoseconds
        std::int64_t last_refill;
    };

    void refill(Bucket& b, std::int64_t now, std::int64_t max_credit) const {
        const std::int64_t elapsed = now - b.last_refill;
        if (elapsed <= 0) return;
        const std::int64_t missing = max_credit - b.credit;
        // Compare before multiplying so elapsed * refill_tokens cannot overflow.
        if (elapsed >= missing / refill_tokens_ + 1) {
            b.credit = max_credit;
        } else {
            b.credit += elapsed * refill_tokens_;
        }
        b.last_refill = now;
    }

    const std::int64_t capacity_;
    const std::int64_t refill_tokens_;
    const std::int64_t period_ns_;
    std::shared_ptr<const Clock> clock_;
    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};

// ---- Sliding window log -----------------------------------------------------
//
// Keeps the timestamp of every accepted request in the last `window`. Exact
// (no boundary bursts like a fixed window) at the cost of O(limit) memory per client.
class SlidingWindowLogLimiter : public RateLimiter {
public:
    SlidingWindowLogLimiter(std::size_t limit, Nanos window, std::shared_ptr<const Clock> clock)
        : limit_(limit), window_(window.count()), clock_(std::move(clock)) {
        if (limit_ == 0 || window_ <= 0) throw std::invalid_argument("limit and window must be positive");
        if (!clock_) throw std::invalid_argument("clock is required");
    }

    bool allow(const std::string& client) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::int64_t now = clock_->now().count();
        std::deque<std::int64_t>& log = logs_[client];
        evict_old(log, now);
        if (log.size() >= limit_) return false;
        log.push_back(now);
        return true;
    }

    // Requests counted in the current window for a client.
    std::size_t in_window(const std::string& client) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = logs_.find(client);
        if (it == logs_.end()) return 0;
        evict_old(it->second, clock_->now().count());
        return it->second.size();
    }

private:
    // A request at time t counts while t > now - window.
    void evict_old(std::deque<std::int64_t>& log, std::int64_t now) const {
        while (!log.empty() && log.front() <= now - window_) log.pop_front();
    }

    const std::size_t limit_;
    const std::int64_t window_;
    std::shared_ptr<const Clock> clock_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::deque<std::int64_t>> logs_;
};

}  // namespace ratelimit
