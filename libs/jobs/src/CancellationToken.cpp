#include <grapple/jobs/CancellationToken.hpp>

namespace grapple::jobs {

void CancellationToken::cancel() noexcept {
  cancelled_ = true;
}

bool CancellationToken::cancelled() const noexcept {
  return cancelled_;
}

} // namespace grapple::jobs

