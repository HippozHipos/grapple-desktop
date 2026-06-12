#pragma once

#include <grapple/runtime/RuntimeCache.hpp>

#include <cstddef>
#include <vector>

namespace grapple::runtime {

class MemoryRuntimeCache final : public IRuntimeCache {
public:
  explicit MemoryRuntimeCache(std::size_t capacity);

  std::optional<RuntimeValue> get(const RuntimeCacheKey& key) override;
  foundation::Result<void> put(const RuntimeCacheKey& key, RuntimeValue value) override;
  foundation::Result<void> invalidate(const std::vector<RuntimeCacheKey>& keys) override;

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t capacity() const noexcept;

private:
  std::size_t capacity_;
  std::vector<std::pair<RuntimeCacheKey, RuntimeValue>> entries_;
};

} // namespace grapple::runtime
