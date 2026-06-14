#pragma once

#include <atomic>
#include <memory>

namespace grapple::jobs {

class CancellationToken {
public:
  CancellationToken();

  void cancel() noexcept;
  [[nodiscard]] bool cancelled() const noexcept;

private:
  std::shared_ptr<std::atomic_bool> cancelled_;
};

} // namespace grapple::jobs
