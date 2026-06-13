#pragma once

#include <grapple/project/ProjectCommandService.hpp>
#include <grapple/project/ProjectQuery.hpp>

namespace grapple::agent {

struct AgentToolContext {
  project::IProjectCommandService& commands;
  project::IProjectQueryService& queries;
};

} // namespace grapple::agent
