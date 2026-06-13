#pragma once

#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/storage/ProjectPackageSession.hpp>
#include <grapple/timeline/EffectPayload.hpp>

#include <string>

namespace grapple::app {

class NativeEffectSession final {
public:
  NativeEffectSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter);

  foundation::Result<storage::ProjectPackageSessionResult> setNumericParam(
    foundation::NodeId effectNodeId,
    std::string paramName,
    double value,
    project::CommandSource source
  );
  foundation::Result<storage::ProjectPackageSessionResult> upsertParamKeyframe(
    foundation::NodeId effectNodeId,
    std::string paramName,
    timeline::Param::Keyframe keyframe,
    project::CommandSource source
  );
  foundation::Result<storage::ProjectPackageSessionResult> deleteParamKeyframe(
    foundation::NodeId effectNodeId,
    std::string paramName,
    foundation::KeyframeId keyframeId,
    project::CommandSource source
  );
  foundation::Result<storage::ProjectPackageSessionResult> deleteEffect(
    foundation::NodeId effectNodeId,
    project::CommandSource source
  );

private:
  NativeProjectSession& project_;
  NativeProjectCommandWriter& commandWriter_;
};

} // namespace grapple::app
