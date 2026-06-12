#pragma once

namespace grapple::jobs {

class IProgressSink {
public:
  virtual ~IProgressSink() = default;

  virtual void reportProgress(double progress) = 0;
};

} // namespace grapple::jobs

