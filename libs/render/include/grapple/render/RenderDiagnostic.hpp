#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <optional>
#include <string>

namespace grapple::render {

enum class DiagnosticSeverity {
  Info,
  Warning,
  Error
};

struct DiagnosticLocation {
  foundation::ProjectId projectId;
  foundation::RevisionId revision;
  std::optional<foundation::NodeId> nodeId;
};

struct RenderDiagnostic {
  std::string code;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  DiagnosticLocation location;
  std::string message;
};

} // namespace grapple::render

