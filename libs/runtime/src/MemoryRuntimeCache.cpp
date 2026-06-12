#include <grapple/runtime/MemoryRuntimeCache.hpp>

#include <algorithm>

namespace grapple::runtime {

std::optional<RuntimeValue> MemoryRuntimeCache::get(const RuntimeCacheKey& key) {
  const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const auto& existing) {
    return existing.first == key;
  });

  if (entry == entries_.end()) {
    return std::nullopt;
  }

  return entry->second;
}

foundation::Result<void> MemoryRuntimeCache::put(const RuntimeCacheKey& key, RuntimeValue value) {
  const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const auto& existing) {
    return existing.first == key;
  });

  if (entry != entries_.end()) {
    entry->second = std::move(value);
    return {};
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

} // namespace grapple::runtime
