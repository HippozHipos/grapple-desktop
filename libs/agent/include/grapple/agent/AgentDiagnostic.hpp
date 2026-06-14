#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <optional>
#include <string>
#include <utility>

namespace grapple::agent {

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

struct AgentDiagnostic {
  AgentDiagnostic(
    std::string codeValue,
    DiagnosticSeverity severityValue,
    DiagnosticLocation locationValue,
    std::string messageValue
  )
    : code{std::move(codeValue)},
      severity{severityValue},
      location{std::move(locationValue)},
      message{std::move(messageValue)} {}

  std::string code;
  DiagnosticSeverity severity;
  DiagnosticLocation location;
  std::string message;
};

} // namespace grapple::agent
