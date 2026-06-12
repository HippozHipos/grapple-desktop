#include <grapple/project/ProjectEventNames.hpp>

#include <cstdlib>

namespace grapple::project {

std::string_view serializedEventName(EventKind kind) {
  switch (kind) {
    case EventKind::ProjectCommandApplied:
      return "project.command_applied";
    case EventKind::ProjectChanged:
      return "project.changed";
  }

  std::abort();
}

} // namespace grapple::project
