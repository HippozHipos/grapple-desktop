#pragma once

#include <grapple/runtime/RuntimeCache.hpp>

#include <vector>

namespace grapple::runtime {

class MemoryRuntimeCache final : public IRuntimeCache {
public:
  std::optional<RuntimeValue> get(const RuntimeCacheKey& key) override;
  foundation::Result<void> put(const RuntimeCacheKey& key, RuntimeValue value) override;
  foundation::Result<void> invalidate(const std::vector<RuntimeCacheKey>& keys) override;

  [[nodiscard]] std::size_t size() const noexcept;

private:
  std::vector<std::pair<RuntimeCacheKey, RuntimeValue>> entries_;
};

bool operator==(const RuntimeCacheKey& left, const RuntimeCacheKey& right);

} // namespace grapple::runtime
