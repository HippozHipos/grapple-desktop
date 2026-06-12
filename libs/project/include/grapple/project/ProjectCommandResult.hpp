#pragma once

#include <grapple/foundation/StrongId.hpp>
#include <grapple/project/ProjectEvents.hpp>

#include <string>
#include <vector>

namespace grapple::project {

struct ProjectDiagnostic {
  std::string code;
  std::string message;
};

struct ProjectCommandResult {
  foundation::CommandId commandId;
  foundation::RevisionId beforeRevision;
  foundation::RevisionId afterRevision;
  std::vector<ProjectEvent> events;
  std::vector<ProjectDiagnostic> diagnostics;
};

} // namespace grapple::project

