#pragma once

#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/storage/ProjectCommitBuilder.hpp>

#include <optional>

namespace grapple::demo {

foundation::Result<void> ensureWalkingWomanDemoVideo();

foundation::Result<void> populateWalkingWomanDemo(
  app::NativeProjectSession& session,
  std::optional<storage::SnapshotCommitRecord> headSnapshot
);

} // namespace grapple::demo
