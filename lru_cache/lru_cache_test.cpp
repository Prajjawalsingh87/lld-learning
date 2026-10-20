#include "lru_cache.hpp"

#include <list>
#include <string>

#include "../testing/mini_test.hpp"

TEST(get_on_empty_cache_misses) {
    LRUCache<int, std::string> cache(2);
    CHECK(!cache.get(1).has_value());
    CHECK_EQ(cache.size(), 0u);
}

TEST(put_then_get_returns_value) {
    LRUCache<int, std::string> cache(2);
    cache.put(1, "one");
    REQUIRE(cache.get(1).has_value());
    CHECK_EQ(*cache.get(1), std::string("one"));
}

TEST(evicts_least_recently_used) {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    auto evicted = cache.put(3, 30);  // 1 is the LRU entry
    REQUIRE(evicted.has_value());
    CHECK_EQ(evicted->first, 1);
    CHECK_EQ(evicted->second, 10);
    CHECK(!cache.contains(1));
    CHECK(cache.contains(2));
    CHECK(cache.contains(3));
}

TEST(get_refreshes_recency) {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    CHECK(cache.get(1).has_value());  // now 2 is the LRU entry
    auto evicted = cache.put(3, 30);
    REQUIRE(evicted.has_value());
    CHECK_EQ(evicted->first, 2);
    CHECK(cache.contains(1));
}

TEST(put_existing_key_updates_and_refreshes) {
    LRUCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    CHECK(!cache.put("a", 100).has_value());  // update, no eviction
    CHECK_EQ(cache.size(), 2u);
    CHECK_EQ(*cache.get("a"), 100);
    cache.put("c", 3);  // "b" is LRU now
    CHECK(!cache.contains("b"));
    CHECK((cache.keys_by_recency() == std::list<std::string>{"c", "a"}));
}

TEST(contains_does_not_change_recency) {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    CHECK(cache.contains(1));  // peek only
    cache.put(3, 30);
    CHECK(!cache.contains(1));
}

TEST(erase_removes_key) {
    LRUCache<int, int> cache(2);
    cache.put(1, 10);
    CHECK(cache.erase(1));
    CHECK(!cache.erase(1));
    CHECK_EQ(cache.size(), 0u);
    CHECK(!cache.get(1).has_value());
}

TEST(capacity_one) {
    LRUCache<int, int> cache(1);
    cache.put(1, 10);
    cache.put(2, 20);
    CHECK(!cache.contains(1));
    CHECK_EQ(*cache.get(2), 20);
}

TEST(zero_capacity_is_rejected) {
    using IntCache = LRUCache<int, int>;
    CHECK_THROWS(IntCache(0), std::invalid_argument);
}

TEST(matches_reference_model_on_long_sequence) {
    // Compare against a simple O(n) reference implementation.
    const std::size_t capacity = 5;
    LRUCache<int, int> cache(capacity);
    std::list<std::pair<int, int>> model;  // front = MRU
    unsigned seed = 12345;
    auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) % 12; };

    for (int step = 0; step < 2000; ++step) {
        int key = static_cast<int>(next());
        auto it = model.begin();
        while (it != model.end() && it->first != key) ++it;
        if (step % 3 == 0) {
            auto got = cache.get(key);
            if (it == model.end()) {
                CHECK(!got.has_value());
            } else {
                REQUIRE(got.has_value());
                CHECK_EQ(*got, it->second);
                model.splice(model.begin(), model, it);
            }
        } else {
            cache.put(key, step);
            if (it != model.end()) model.erase(it);
            model.emplace_front(key, step);
            if (model.size() > capacity) model.pop_back();
        }
        REQUIRE_EQ(cache.size(), model.size());
    }
    std::list<int> model_keys;
    for (auto& e : model) model_keys.push_back(e.first);
    CHECK((cache.keys_by_recency() == model_keys));
}

MINI_TEST_MAIN()
