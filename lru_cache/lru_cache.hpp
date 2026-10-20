// LRU (least recently used) cache with O(1) get and put.
//
// A doubly linked list keeps entries ordered from most recently used (front)
// to least recently used (back). A hash map points from each key to its list
// node, so we can find a node in O(1) and move it to the front in O(1) with
// std::list::splice (which relinks the node without copying it).
#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class LRUCache {
public:
    using Entry = std::pair<Key, Value>;

    explicit LRUCache(std::size_t capacity) : capacity_(capacity) {
        if (capacity_ == 0) throw std::invalid_argument("LRUCache capacity must be > 0");
    }

    // Returns the value and marks the key as most recently used.
    std::optional<Value> get(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;
        touch(it->second);
        return it->second->second;
    }

    // Inserts or updates a key. If the cache was full, the least recently used
    // entry is evicted and returned so callers can react (e.g. write it back).
    std::optional<Entry> put(const Key& key, Value value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(value);
            touch(it->second);
            return std::nullopt;
        }

        std::optional<Entry> evicted;
        if (entries_.size() == capacity_) {
            evicted = std::move(entries_.back());
            index_.erase(evicted->first);
            entries_.pop_back();
        }
        entries_.emplace_front(key, std::move(value));
        index_[key] = entries_.begin();
        return evicted;
    }

    // Removes a key. Returns true if it was present.
    bool erase(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return false;
        entries_.erase(it->second);
        index_.erase(it);
        return true;
    }

    // Does not change recency (a "peek").
    bool contains(const Key& key) const { return index_.count(key) != 0; }

    std::size_t size() const { return entries_.size(); }
    std::size_t capacity() const { return capacity_; }

    // Keys from most to least recently used. O(n), meant for tests and debugging.
    std::list<Key> keys_by_recency() const {
        std::list<Key> keys;
        for (const Entry& e : entries_) keys.push_back(e.first);
        return keys;
    }

private:
    using ListIt = typename std::list<Entry>::iterator;

    void touch(ListIt node) { entries_.splice(entries_.begin(), entries_, node); }

    std::size_t capacity_;
    std::list<Entry> entries_;  // front = most recently used
    std::unordered_map<Key, ListIt, Hash> index_;
};
