#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>

namespace grapple::jobs {

using MainThreadTask = std::function<void()>;

class MainThreadDispatcher {
public:
  void post(MainThreadTask task);
  std::size_t drain();

  [[nodiscard]] std::size_t size() const;

private:
  mutable std::mutex mutex_;
  std::deque<MainThreadTask> tasks_;
};

} // namespace grapple::jobs
