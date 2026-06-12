#pragma once

#include <grapple/foundation/StrongId.hpp>

#include <variant>

namespace grapple::project {

enum class EventKind {
  ProjectCommandApplied,
  ProjectChanged
};

struct ProjectCommandAppliedEvent {
  foundation::CommandId commandId;
  foundation::RevisionId beforeRevision;
  foundation::RevisionId afterRevision;
};

struct ProjectChangedEvent {
  foundation::ProjectId projectId;
  foundation::RevisionId beforeRevision;
  foundation::RevisionId afterRevision;
};

using ProjectEvent = std::variant<
  ProjectCommandAppliedEvent,
  ProjectChangedEvent
>;

[[nodiscard]] EventKind eventKind(const ProjectEvent& event);

} // namespace grapple::project
