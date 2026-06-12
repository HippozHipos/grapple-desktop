#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/jobs/CancellationToken.hpp>
#include <grapple/jobs/ProgressSink.hpp>

#include <functional>
#include <string>

namespace grapple::jobs {

using JobHandler = std::function<foundation::Result<void>(
  CancellationToken& cancellation,
  IProgressSink& progress
)>;

struct Job {
  foundation::JobId id;
  std::string name;
  JobHandler handler;
};

} // namespace grapple::jobs

