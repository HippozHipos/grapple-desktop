#pragma once

#include <grapple/model/ModelService.hpp>
#include <grapple/project/ProjectCommandService.hpp>
#include <grapple/project/ProjectQuery.hpp>

namespace grapple::agent {

struct AgentToolContext {
  project::IProjectCommandService& commands;
  project::IProjectQueryService& queries;
  model::IModelService& models;
};

} // namespace grapple::agent
