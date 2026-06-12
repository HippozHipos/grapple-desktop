#include <grapple/project/ProjectCommandNames.hpp>

#include <cstdlib>

namespace grapple::project {

std::string_view serializedCommandName(CommandKind kind) {
  switch (kind) {
    case CommandKind::CreateComposition:
      return "project.create_composition";
    case CommandKind::CreateTrack:
      return "project.create_track";
    case CommandKind::CreateClip:
      return "project.create_clip";
    case CommandKind::CreateCamera:
      return "project.create_camera";
    case CommandKind::CreateEffect:
      return "project.create_effect";
    case CommandKind::RestoreSnapshot:
      return "project.restore_snapshot";
  }

  std::abort();
}

} // namespace grapple::project
