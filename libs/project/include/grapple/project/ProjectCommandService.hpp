#pragma once

#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectCommandResult.hpp>

namespace grapple::project {

class IProjectCommandService {
public:
  virtual ~IProjectCommandService() = default;

  virtual foundation::Result<ProjectCommandResult> apply(
    const ProjectCommandEnvelope& command
  ) = 0;
};

} // namespace grapple::project

