#include <grapple/jobs/MainThreadDispatcher.hpp>

#include <utility>

namespace grapple::jobs {

void MainThreadDispatcher::post(MainThreadTask task) {
  std::lock_guard lock{mutex_};
  tasks_.push_back(std::move(task));
}

std::size_t MainThreadDispatcher::drain() {
  std::deque<MainThreadTask> tasks;
  {
    std::lock_guard lock{mutex_};
    tasks = std::move(tasks_);
    tasks_.clear();
  }

  const std::size_t drained = tasks.size();
  for (MainThreadTask& task : tasks) {
    task();
  }
  return drained;
}

std::size_t MainThreadDispatcher::size() const {
  std::lock_guard lock{mutex_};
  return tasks_.size();
}

} // namespace grapple::jobs
