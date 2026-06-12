#include <grapple/project/ProjectEvents.hpp>

#include <type_traits>

namespace grapple::project {

EventKind eventKind(const ProjectEvent& event) {
  return std::visit(
    [](const auto& typedEvent) -> EventKind {
      using Event = std::decay_t<decltype(typedEvent)>;
      if constexpr (std::is_same_v<Event, ProjectCommandAppliedEvent>) {
        return EventKind::ProjectCommandApplied;
      } else if constexpr (std::is_same_v<Event, ProjectChangedEvent>) {
        return EventKind::ProjectChanged;
      }
    },
    event
  );
}

} // namespace grapple::project
