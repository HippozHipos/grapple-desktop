#include <grapple/runtime/MemoryRuntimeCache.hpp>

#include <algorithm>

namespace grapple::runtime {

bool operator==(const RuntimeCacheKey& left, const RuntimeCacheKey& right) {
  return left.planHash == right.planHash &&
         left.nodeId == right.nodeId &&
         left.implementationHash == right.implementationHash &&
         left.paramsHash == right.paramsHash &&
         left.inputsHash == right.inputsHash &&
         left.assetId == right.assetId &&
         left.assetVersionHash == right.assetVersionHash &&
         left.modelId == right.modelId &&
         left.modelVersion == right.modelVersion &&
         left.range == right.range &&
         left.runtimeVersion == right.runtimeVersion;
}

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

std::size_t MemoryRuntimeCache::size() const noexcept {
  return entries_.size();
}

} // namespace grapple::runtime

