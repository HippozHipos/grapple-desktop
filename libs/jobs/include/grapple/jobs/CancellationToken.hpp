#pragma once

namespace grapple::jobs {

class CancellationToken {
public:
  void cancel() noexcept;
  [[nodiscard]] bool cancelled() const noexcept;

private:
  bool cancelled_ = false;
};

} // namespace grapple::jobs

