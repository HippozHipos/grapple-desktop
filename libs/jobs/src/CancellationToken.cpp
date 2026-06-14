#include <grapple/jobs/CancellationToken.hpp>

namespace grapple::jobs {

CancellationToken::CancellationToken()
  : cancelled_{std::make_shared<std::atomic_bool>(false)} {}

void CancellationToken::cancel() noexcept {
  cancelled_->store(true);
}

bool CancellationToken::cancelled() const noexcept {
  return cancelled_->load();
}

} // namespace grapple::jobs
