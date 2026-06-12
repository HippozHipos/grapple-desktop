#include <grapple/runtime/MemoryRuntimeCache.hpp>

#include <algorithm>

namespace grapple::runtime {

MemoryRuntimeCache::MemoryRuntimeCache(std::size_t capacity)
  : capacity_{capacity} {}

std::optional<RuntimeValue> MemoryRuntimeCache::get(const RuntimeCacheKey& key) {
  const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const auto& existing) {
    return existing.first == key;
  });

  if (entry == entries_.end()) {
    return std::nullopt;
  }

  auto cached = std::move(*entry);
  RuntimeValue value = cached.second;
  entries_.erase(entry);
  entries_.push_back(std::move(cached));
  return value;
}

foundation::Result<void> MemoryRuntimeCache::put(const RuntimeCacheKey& key, RuntimeValue value) {
  if (capacity_ == 0) {
    return foundation::Error{"runtime.cache_capacity_empty", "Runtime cache capacity must be greater than zero."};
  }

  const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const auto& existing) {
    return existing.first == key;
  });

  if (entry != entries_.end()) {
    const RuntimeCacheKey existingKey = entry->first;
    entries_.erase(entry);
    entries_.push_back(std::make_pair(existingKey, std::move(value)));
    return {};
  }

  if (entries_.size() == capacity_) {
    entries_.erase(entries_.begin());
  }

  entries_.push_back(std::make_pair(key, std::move(value)));
  return {};
}

foundation::Result<void> MemoryRuntimeCache::invalidate(const std::vector<RuntimeCacheKey>& keys) {
  for (const RuntimeCacheKey& key : keys) {
    entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(), [&](const auto& existing) {
        return existing.first == key;
      }),
      entries_.end()
    );
  }
  return {};
}

std::size_t MemoryRuntimeCache::size() const noexcept {
  return entries_.size();
}

std::size_t MemoryRuntimeCache::capacity() const noexcept {
  return capacity_;
}

} // namespace grapple::runtime
