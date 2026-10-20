#include "rate_limiter.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "../testing/mini_test.hpp"

using namespace ratelimit;
using std::chrono::milliseconds;
using std::chrono::seconds;

namespace {

int count_allowed(RateLimiter& limiter, const std::string& client, int attempts) {
    int allowed = 0;
    for (int i = 0; i < attempts; ++i)
        if (limiter.allow(client)) ++allowed;
    return allowed;
}

}  // namespace

// ---- Token bucket ----

TEST(token_bucket_allows_burst_up_to_capacity) {
    auto clock = std::make_shared<ManualClock>();
    TokenBucketLimiter limiter(5, 1, seconds(1), clock);
    CHECK_EQ(count_allowed(limiter, "alice", 10), 5);
    CHECK(!limiter.allow("alice"));
}

TEST(token_bucket_refills_over_time) {
    auto clock = std::make_shared<ManualClock>();
    TokenBucketLimiter limiter(5, 1, seconds(1), clock);  // 1 token per second
    count_allowed(limiter, "alice", 5);
    clock->advance(milliseconds(999));
    CHECK(!limiter.allow("alice"));  // not quite one token yet
    clock->advance(milliseconds(1));
    CHECK(limiter.allow("alice"));
    CHECK(!limiter.allow("alice"));
    clock->advance(seconds(3));
    CHECK_EQ(limiter.available("alice"), 3);
}

TEST(token_bucket_never_exceeds_capacity) {
    auto clock = std::make_shared<ManualClock>();
    TokenBucketLimiter limiter(3, 1, seconds(1), clock);
    limiter.allow("alice");
    clock->advance(seconds(1000000));  // a very long idle time must not overflow
    CHECK_EQ(limiter.available("alice"), 3);
    CHECK_EQ(count_allowed(limiter, "alice", 10), 3);
}

TEST(token_bucket_fractional_rate_is_exact) {
    auto clock = std::make_shared<ManualClock>();
    TokenBucketLimiter limiter(1, 3, seconds(1), clock);  // 3 tokens/s => one every 333.33 ms
    CHECK(limiter.allow("bob"));
    clock->advance(milliseconds(333));
    CHECK(!limiter.allow("bob"));
    clock->advance(milliseconds(1));
    CHECK(limiter.allow("bob"));
}

TEST(token_bucket_clients_are_independent) {
    auto clock = std::make_shared<ManualClock>();
    TokenBucketLimiter limiter(2, 1, seconds(1), clock);
    CHECK_EQ(count_allowed(limiter, "alice", 5), 2);
    CHECK_EQ(count_allowed(limiter, "bob", 5), 2);
}

TEST(token_bucket_rejects_bad_config) {
    auto clock = std::make_shared<ManualClock>();
    CHECK_THROWS(TokenBucketLimiter(0, 1, seconds(1), clock), std::invalid_argument);
    CHECK_THROWS(TokenBucketLimiter(1, 0, seconds(1), clock), std::invalid_argument);
    CHECK_THROWS(TokenBucketLimiter(1, 1, seconds(1), nullptr), std::invalid_argument);
}

// ---- Sliding window log ----

TEST(sliding_window_allows_limit_per_window) {
    auto clock = std::make_shared<ManualClock>();
    SlidingWindowLogLimiter limiter(3, seconds(10), clock);
    CHECK_EQ(count_allowed(limiter, "alice", 5), 3);
    CHECK_EQ(limiter.in_window("alice"), 3u);
}

TEST(sliding_window_rolls_continuously) {
    auto clock = std::make_shared<ManualClock>();
    SlidingWindowLogLimiter limiter(3, seconds(10), clock);
    CHECK(limiter.allow("a"));  // t=0
    clock->advance(seconds(4));
    CHECK(limiter.allow("a"));  // t=4
    clock->advance(seconds(4));
    CHECK(limiter.allow("a"));  // t=8
    CHECK(!limiter.allow("a"));
    clock->advance(seconds(2));  // t=10: request from t=0 leaves the window
    CHECK(limiter.allow("a"));
    CHECK(!limiter.allow("a"));
    clock->advance(seconds(4));  // t=14: t=4 leaves
    CHECK(limiter.allow("a"));
    CHECK_EQ(limiter.in_window("a"), 3u);
}

TEST(sliding_window_has_no_fixed_window_boundary_burst) {
    // A fixed window counter would allow 3 at t=9.9s and 3 more at t=10s.
    auto clock = std::make_shared<ManualClock>();
    SlidingWindowLogLimiter limiter(3, seconds(10), clock);
    clock->advance(milliseconds(9900));
    CHECK_EQ(count_allowed(limiter, "a", 3), 3);
    clock->advance(milliseconds(100));
    CHECK_EQ(count_allowed(limiter, "a", 3), 0);
}

TEST(rejected_requests_do_not_consume_the_window) {
    auto clock = std::make_shared<ManualClock>();
    SlidingWindowLogLimiter limiter(1, seconds(1), clock);
    CHECK(limiter.allow("a"));
    for (int i = 0; i < 100; ++i) limiter.allow("a");
    clock->advance(seconds(1));
    CHECK(limiter.allow("a"));
}

// ---- Both, through the interface ----

TEST(polymorphic_use_through_interface) {
    auto clock = std::make_shared<ManualClock>();
    std::vector<std::unique_ptr<RateLimiter>> limiters;
    limiters.push_back(std::make_unique<TokenBucketLimiter>(4, 4, seconds(1), clock));
    limiters.push_back(std::make_unique<SlidingWindowLogLimiter>(4, seconds(1), clock));
    for (auto& limiter : limiters) CHECK_EQ(count_allowed(*limiter, "x", 10), 4);
}

TEST(thread_safety_exact_count_under_contention) {
    // Time is frozen, so across all threads exactly `capacity` requests may pass.
    auto clock = std::make_shared<ManualClock>();
    TokenBucketLimiter bucket(1000, 1, seconds(1), clock);
    SlidingWindowLogLimiter window(1000, seconds(1), clock);
    std::atomic<int> bucket_allowed{0}, window_allowed{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 500; ++i) {
                if (bucket.allow("shared")) ++bucket_allowed;
                if (window.allow("shared")) ++window_allowed;
            }
        });
    }
    for (auto& th : threads) th.join();
    CHECK_EQ(bucket_allowed.load(), 1000);
    CHECK_EQ(window_allowed.load(), 1000);
}

TEST(works_with_real_clock) {
    TokenBucketLimiter limiter(2, 1, seconds(60), std::make_shared<SteadyClock>());
    CHECK_EQ(count_allowed(limiter, "a", 5), 2);
}

MINI_TEST_MAIN()
