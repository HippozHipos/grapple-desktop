#pragma once

#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/storage/ProjectPackageSession.hpp>

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
  foundation::Result<storage::ProjectPackageSessionResult> deleteEffect(
    foundation::NodeId effectNodeId,
    project::CommandSource source
  );

private:
  NativeProjectSession& project_;
  NativeProjectCommandWriter& commandWriter_;
};

} // namespace grapple::app
