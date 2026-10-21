// Port of Sources/FotufilmCore/BoundedCache.swift.
#ifndef FOTUFILM_CORE_BOUNDED_CACHE_H
#define FOTUFILM_CORE_BOUNDED_CACHE_H

#include <algorithm>
#include <cstddef>
#include <deque>
#include <optional>
#include <unordered_map>

#include "Simd.h"

namespace fotufilm {

/// A memo table that forgets: past `limit` entries the least recently used one goes.
/// Not thread-safe on its own; every table sits behind its own lock.
template <typename Key, typename Value, typename Hash = std::hash<Key>>
class BoundedCache {
public:
    explicit BoundedCache(size_t limit) : limit_(limit) {
        preconditionCheck(limit > 0, "a cache that holds nothing is a cache of nothing");
    }

    size_t limit() const { return limit_; }
    size_t count() const { return values_.size(); }

    template <typename F>
    void forEachValue(F&& f) const {
        for (const auto& entry : values_) f(entry.second);
    }

    /// The value for `key`, and a note that it was wanted just now.
    std::optional<Value> value(const Key& key) {
        auto found = values_.find(key);
        if (found == values_.end()) return std::nullopt;
        touch(key);
        return found->second;
    }

    /// Keeps `value` under `key`, letting the least recently used entry go when the table is full.
    void insert(const Value& value, const Key& key) {
        auto found = values_.find(key);
        if (found != values_.end()) {
            found->second = value;
            touch(key);
            return;
        }
        values_.emplace(key, value);
        recency_.push_back(key);
        while (values_.size() > limit_ && !recency_.empty()) {
            Key oldest = recency_.front();
            recency_.pop_front();
            values_.erase(oldest);
        }
    }

    void removeAll() {
        values_.clear();
        recency_.clear();
    }

private:
    void touch(const Key& key) {
        auto place = std::find(recency_.rbegin(), recency_.rend(), key);
        if (place == recency_.rend() || place == recency_.rbegin()) return;
        recency_.erase(std::next(place).base());
        recency_.push_back(key);
    }

    size_t limit_;
    std::unordered_map<Key, Value, Hash> values_;
    /// Keys from least to most recently used.
    std::deque<Key> recency_;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_BOUNDED_CACHE_H
