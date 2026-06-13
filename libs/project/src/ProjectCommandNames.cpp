#include <grapple/project/ProjectCommandNames.hpp>

#include <cstdlib>

namespace grapple::project {

std::string_view serializedCommandName(CommandKind kind) {
  switch (kind) {
    case CommandKind::RegisterAsset:
      return "project.register_asset";
    case CommandKind::CreateComposition:
      return "project.create_composition";
    case CommandKind::CreateTrack:
      return "project.create_track";
    case CommandKind::CreateClip:
      return "project.create_clip";
    case CommandKind::UpdateClip:
      return "project.update_clip";
    case CommandKind::DeleteClip:
      return "project.delete_clip";
    case CommandKind::CreateCamera:
      return "project.create_camera";
    case CommandKind::UpdateCamera:
      return "project.update_camera";
    case CommandKind::CreateEffect:
      return "project.create_effect";
    case CommandKind::DeleteEffect:
      return "project.delete_effect";
    case CommandKind::ConnectNodes:
      return "project.connect_nodes";
    case CommandKind::DisconnectNodes:
      return "project.disconnect_nodes";
    case CommandKind::SetEffectParams:
      return "project.set_effect_params";
    case CommandKind::RestoreSnapshot:
      return "project.restore_snapshot";
  }

  std::abort();
}

std::string_view serializedCommandSourceKind(CommandSourceKind kind) {
  switch (kind) {
    case CommandSourceKind::User:
      return "user";
    case CommandSourceKind::Agent:
      return "agent";
    case CommandSourceKind::Importer:
      return "importer";
    case CommandSourceKind::Migration:
      return "migration";
  }

  std::abort();
}

} // namespace grapple::project
