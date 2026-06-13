#include <grapple/app/NativeEffectSession.hpp>

#include <utility>

namespace grapple::app {

NativeEffectSession::NativeEffectSession(NativeProjectSession& project, NativeProjectCommandWriter& commandWriter)
  : project_{project},
    commandWriter_{commandWriter} {}

foundation::Result<storage::ProjectPackageSessionResult> NativeEffectSession::setNumericParam(
  foundation::NodeId effectNodeId,
  std::string paramName,
  double value,
  project::CommandSource source
) {
  return commandWriter_.apply(
    project::UpdateEffectParamValueCommand{
      std::move(effectNodeId),
      std::move(paramName),
      timeline::ParamValue{value}
    },
    std::move(source)
  );
}

foundation::Result<storage::ProjectPackageSessionResult> NativeEffectSession::upsertParamKeyframe(
  foundation::NodeId effectNodeId,
  std::string paramName,
  timeline::Param::Keyframe keyframe,
  project::CommandSource source
) {
  return commandWriter_.apply(
    project::UpsertEffectParamKeyframeCommand{
      std::move(effectNodeId),
      std::move(paramName),
      std::move(keyframe)
    },
    std::move(source)
  );
}

foundation::Result<storage::ProjectPackageSessionResult> NativeEffectSession::deleteParamKeyframe(
  foundation::NodeId effectNodeId,
  std::string paramName,
  foundation::KeyframeId keyframeId,
  project::CommandSource source
) {
  return commandWriter_.apply(
    project::DeleteEffectParamKeyframeCommand{
      std::move(effectNodeId),
      std::move(paramName),
      std::move(keyframeId)
    },
    std::move(source)
  );
}

foundation::Result<storage::ProjectPackageSessionResult> NativeEffectSession::deleteEffect(
  foundation::NodeId effectNodeId,
  project::CommandSource source
) {
  return commandWriter_.apply(
    project::DeleteEffectCommand{std::move(effectNodeId)},
    std::move(source)
  );
}

} // namespace grapple::app
