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
    case CommandKind::DeleteTrack:
      return "project.delete_track";
    case CommandKind::CreateClip:
      return "project.create_clip";
    case CommandKind::MoveClip:
      return "project.move_clip";
    case CommandKind::TrimClip:
      return "project.trim_clip";
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
    case CommandKind::ConnectPorts:
      return "project.connect_ports";
    case CommandKind::DisconnectPorts:
      return "project.disconnect_ports";
    case CommandKind::UpdateEffectParamValue:
      return "project.update_effect_param_value";
    case CommandKind::UpsertEffectParamKeyframe:
      return "project.upsert_effect_param_keyframe";
    case CommandKind::DeleteEffectParamKeyframe:
      return "project.delete_effect_param_keyframe";
    case CommandKind::CreateNote:
      return "project.create_note";
    case CommandKind::UpdateNote:
      return "project.update_note";
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
