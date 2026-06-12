#pragma once

#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/storage/ProjectPackageSession.hpp>

namespace grapple::app {

class NativeStewardSession final {
public:
  NativeStewardSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter);

  foundation::Result<storage::ProjectPackageSessionResult> createCameraTransformEffect(
    foundation::NodeId cameraNodeId,
    foundation::TimeRange activeRange
  );

private:
  NativeProjectSession& project_;
  NativeProjectCommandWriter& commandWriter_;
};

} // namespace grapple::app
