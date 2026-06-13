#include <grapple/storage/ProjectPackageManifest.hpp>

#include <grapple/foundation/Json.hpp>

#include <json/json.h>

#include <initializer_list>
#include <memory>
#include <sstream>
#include <string_view>

namespace grapple::storage {

namespace {

void writeOptionalId(std::ostringstream& stream, const char* key, const std::optional<foundation::CommandId>& id) {
  stream << foundation::jsonQuoted(key) << ':';
  if (id.has_value()) {
    stream << foundation::jsonQuoted(id->value());
  } else {
    stream << "null";
  }
}

void writeOptionalId(std::ostringstream& stream, const char* key, const std::optional<foundation::SnapshotId>& id) {
  stream << foundation::jsonQuoted(key) << ':';
  if (id.has_value()) {
    stream << foundation::jsonQuoted(id->value());
  } else {
    stream << "null";
  }
}

const history::SnapshotRecord* findSnapshotById(
  const history::SnapshotStore& snapshots,
  const foundation::SnapshotId& id
) {
  for (const history::SnapshotRecord& snapshot : snapshots.records()) {
    if (snapshot.id == id) {
      return &snapshot;
    }
  }
  return nullptr;
}

foundation::Error parseError(const std::string& path, const std::string& message) {
  return foundation::Error{"storage.manifest_json_invalid", path + ": " + message};
}

foundation::Result<Json::Value> parseJson(const std::string& json) {
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
    return parseError("$", "Invalid JSON. " + errors);
  }
  if (!root.isObject()) {
    return parseError("$", "Package manifest must be a JSON object.");
  }
  return root;
}

foundation::Result<Json::Value> requiredMember(const Json::Value& object, const char* key, const std::string& path) {
  if (!object.isObject()) {
    return parseError(path, "Expected object.");
  }
  if (!object.isMember(key)) {
    return parseError(path + "." + key, "Missing required field.");
  }
  return object[key];
}

foundation::Result<void> requireOnlyMembers(
  const Json::Value& object,
  std::initializer_list<std::string_view> allowed,
  const std::string& path
) {
  if (!object.isObject()) {
    return parseError(path, "Expected object.");
  }

  for (const std::string& member : object.getMemberNames()) {
    bool expected = false;
    for (std::string_view allowedMember : allowed) {
      if (member == allowedMember) {
        expected = true;
        break;
      }
    }
    if (!expected) {
      return parseError(path + "." + member, "Unexpected serialized field.");
    }
  }

  return {};
}

foundation::Result<std::string> requiredStringMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string.");
  }
  if (value.value().asString().empty()) {
    return parseError(path + "." + key, "Expected non-empty string.");
  }
  return value.value().asString();
}

foundation::Result<int> requiredIntMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isIntegral()) {
    return parseError(path + "." + key, "Expected integer.");
  }
  return value.value().asInt();
}

foundation::Result<std::optional<foundation::CommandId>> optionalCommandIdMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (value.value().isNull()) {
    return std::optional<foundation::CommandId>{};
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string or null.");
  }
  if (value.value().asString().empty()) {
    return parseError(path + "." + key, "Expected non-empty string or null.");
  }
  return std::optional<foundation::CommandId>{foundation::CommandId{value.value().asString()}};
}

foundation::Result<std::optional<foundation::SnapshotId>> optionalSnapshotIdMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (value.value().isNull()) {
    return std::optional<foundation::SnapshotId>{};
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string or null.");
  }
  if (value.value().asString().empty()) {
    return parseError(path + "." + key, "Expected non-empty string or null.");
  }
  return std::optional<foundation::SnapshotId>{foundation::SnapshotId{value.value().asString()}};
}

foundation::Result<std::optional<std::string>> optionalStringMember(
  const Json::Value& object,
  const char* key,
  const std::string& path
) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (value.value().isNull()) {
    return std::optional<std::string>{};
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string or null.");
  }
  return std::optional<std::string>{value.value().asString()};
}

foundation::Result<ProjectPackageHeadManifest> parseHeadManifest(const Json::Value& object, const std::string& path) {
  auto members = requireOnlyMembers(object, {"revision", "lastCommandId", "lastSnapshotId"}, path);
  if (!members) {
    return members.error();
  }
  auto revision = requiredStringMember(object, "revision", path);
  if (!revision) {
    return revision.error();
  }
  auto lastCommandId = optionalCommandIdMember(object, "lastCommandId", path);
  if (!lastCommandId) {
    return lastCommandId.error();
  }
  auto lastSnapshotId = optionalSnapshotIdMember(object, "lastSnapshotId", path);
  if (!lastSnapshotId) {
    return lastSnapshotId.error();
  }
  return ProjectPackageHeadManifest{
    foundation::RevisionId{revision.value()},
    lastCommandId.value(),
    lastSnapshotId.value()
  };
}

foundation::Result<ProjectPackageSnapshotManifest> parseSnapshotManifest(const Json::Value& object, const std::string& path) {
  auto members = requireOnlyMembers(object, {"id", "revision", "canonicalHash", "documentPath", "label"}, path);
  if (!members) {
    return members.error();
  }
  auto id = requiredStringMember(object, "id", path);
  if (!id) {
    return id.error();
  }
  auto revision = requiredStringMember(object, "revision", path);
  if (!revision) {
    return revision.error();
  }
  auto hashHex = requiredStringMember(object, "canonicalHash", path);
  if (!hashHex) {
    return hashHex.error();
  }
  const std::optional<foundation::Hash256> canonicalHash = foundation::hashFromHex(hashHex.value());
  if (!canonicalHash.has_value()) {
    return parseError(path + ".canonicalHash", "Hash must be 64 hex characters.");
  }
  auto documentPath = requiredStringMember(object, "documentPath", path);
  if (!documentPath) {
    return documentPath.error();
  }
  auto label = optionalStringMember(object, "label", path);
  if (!label) {
    return label.error();
  }
  return ProjectPackageSnapshotManifest{
    foundation::SnapshotId{id.value()},
    foundation::RevisionId{revision.value()},
    canonicalHash.value(),
    foundation::FilePath{documentPath.value()},
    label.value()
  };
}

} // namespace

foundation::Result<ProjectPackageManifest> buildProjectPackageManifest(const ProjectPackageState& state) {
  if (!state.package.projectId) {
    return foundation::Error{"storage.package_project_id_empty", "Project package id must not be empty."};
  }

  ProjectPackageManifest manifest;
  manifest.projectId = state.package.projectId;
  manifest.schemaVersion = state.package.schemaVersion;
  manifest.commandLogPath = foundation::FilePath{"history/commands.json"};
  manifest.eventLogPath = foundation::FilePath{"history/events.json"};
  manifest.schemaMigrationLogPath = foundation::FilePath{"history/schema_migrations.json"};

  if (!state.head.has_value()) {
    return manifest;
  }

  manifest.head = ProjectPackageHeadManifest{
    state.head->currentRevision,
    state.head->lastCommandId,
    state.head->lastSnapshotId
  };

  if (!state.head->lastSnapshotId.has_value()) {
    return manifest;
  }

  const history::SnapshotRecord* snapshot = findSnapshotById(state.snapshots, *state.head->lastSnapshotId);
  if (snapshot == nullptr) {
    return foundation::Error{"storage.package_head_snapshot_missing", "Package head references a missing snapshot record."};
  }

  manifest.latestSnapshot = ProjectPackageSnapshotManifest{
    snapshot->id,
    snapshot->revision,
    snapshot->canonicalHash,
    snapshot->documentPath,
    snapshot->label
  };
  return manifest;
}

std::string serializeCanonicalProjectPackageManifest(const ProjectPackageManifest& manifest) {
  std::ostringstream stream;
  stream << '{';
  stream << "\"schemaVersion\":" << manifest.schemaVersion;
  stream << ',';
  foundation::writeJsonStringProperty(stream, "projectId", manifest.projectId.value());
  stream << ',';
  foundation::writeJsonStringProperty(stream, "commandLogPath", manifest.commandLogPath.value);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "eventLogPath", manifest.eventLogPath.value);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "schemaMigrationLogPath", manifest.schemaMigrationLogPath.value);
  stream << ",\"head\":";
  if (manifest.head.has_value()) {
    stream << '{';
    foundation::writeJsonStringProperty(stream, "revision", manifest.head->revision.value());
    stream << ',';
    writeOptionalId(stream, "lastCommandId", manifest.head->lastCommandId);
    stream << ',';
    writeOptionalId(stream, "lastSnapshotId", manifest.head->lastSnapshotId);
    stream << '}';
  } else {
    stream << "null";
  }

  stream << ",\"latestSnapshot\":";
  if (manifest.latestSnapshot.has_value()) {
    stream << '{';
    foundation::writeJsonStringProperty(stream, "id", manifest.latestSnapshot->id.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "revision", manifest.latestSnapshot->revision.value());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "canonicalHash", manifest.latestSnapshot->canonicalHash.toHex());
    stream << ',';
    foundation::writeJsonStringProperty(stream, "documentPath", manifest.latestSnapshot->documentPath.value);
    stream << ",\"label\":";
    if (manifest.latestSnapshot->label.has_value()) {
      stream << foundation::jsonQuoted(*manifest.latestSnapshot->label);
    } else {
      stream << "null";
    }
    stream << '}';
  } else {
    stream << "null";
  }
  stream << '}';
  return stream.str();
}

foundation::Result<ProjectPackageManifest> deserializeCanonicalProjectPackageManifest(const std::string& json) {
  auto root = parseJson(json);
  if (!root) {
    return root.error();
  }
  auto members = requireOnlyMembers(
    root.value(),
    {"schemaVersion", "projectId", "commandLogPath", "eventLogPath", "schemaMigrationLogPath", "head", "latestSnapshot"},
    "$"
  );
  if (!members) {
    return members.error();
  }

  auto schemaVersion = requiredIntMember(root.value(), "schemaVersion", "$");
  if (!schemaVersion) {
    return schemaVersion.error();
  }
  auto projectId = requiredStringMember(root.value(), "projectId", "$");
  if (!projectId) {
    return projectId.error();
  }
  auto headValue = requiredMember(root.value(), "head", "$");
  if (!headValue) {
    return headValue.error();
  }
  auto snapshotValue = requiredMember(root.value(), "latestSnapshot", "$");
  if (!snapshotValue) {
    return snapshotValue.error();
  }

  ProjectPackageManifest manifest;
  manifest.schemaVersion = schemaVersion.value();
  manifest.projectId = foundation::ProjectId{projectId.value()};
  auto commandLogPath = requiredStringMember(root.value(), "commandLogPath", "$");
  if (!commandLogPath) {
    return commandLogPath.error();
  }
  auto eventLogPath = requiredStringMember(root.value(), "eventLogPath", "$");
  if (!eventLogPath) {
    return eventLogPath.error();
  }
  auto schemaMigrationLogPath = requiredStringMember(root.value(), "schemaMigrationLogPath", "$");
  if (!schemaMigrationLogPath) {
    return schemaMigrationLogPath.error();
  }
  manifest.commandLogPath = foundation::FilePath{commandLogPath.value()};
  manifest.eventLogPath = foundation::FilePath{eventLogPath.value()};
  manifest.schemaMigrationLogPath = foundation::FilePath{schemaMigrationLogPath.value()};

  if (!headValue.value().isNull()) {
    if (!headValue.value().isObject()) {
      return parseError("$.head", "Expected object or null.");
    }
    auto head = parseHeadManifest(headValue.value(), "$.head");
    if (!head) {
      return head.error();
    }
    manifest.head = head.value();
  }

  if (!snapshotValue.value().isNull()) {
    if (!snapshotValue.value().isObject()) {
      return parseError("$.latestSnapshot", "Expected object or null.");
    }
    auto snapshot = parseSnapshotManifest(snapshotValue.value(), "$.latestSnapshot");
    if (!snapshot) {
      return snapshot.error();
    }
    manifest.latestSnapshot = snapshot.value();
  }

  return manifest;
}

} // namespace grapple::storage
