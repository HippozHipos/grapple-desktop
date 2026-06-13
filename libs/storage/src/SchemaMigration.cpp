#include <grapple/storage/SchemaMigration.hpp>

#include <grapple/foundation/Json.hpp>

#include <json/json.h>

#include <chrono>
#include <memory>
#include <sstream>
#include <utility>

namespace grapple::storage {

namespace {

std::int64_t millisecondsSinceEpoch(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::chrono::system_clock::time_point timePointFromMilliseconds(std::int64_t milliseconds) {
  return std::chrono::system_clock::time_point{std::chrono::milliseconds{milliseconds}};
}

foundation::Result<void> validateRecord(const SchemaMigrationRecord& record) {
  if (record.operationName.empty()) {
    return foundation::Error{"storage.schema_migration_operation_empty", "Schema migration operation name must not be empty."};
  }
  if (record.fromSchemaVersion <= 0 || record.toSchemaVersion <= 0) {
    return foundation::Error{"storage.schema_migration_version_invalid", "Schema migration versions must be positive."};
  }
  if (record.fromSchemaVersion == record.toSchemaVersion) {
    return foundation::Error{"storage.schema_migration_version_unchanged", "Schema migration must record a version change."};
  }
  return {};
}

foundation::Error parseError(const std::string& path, const std::string& message) {
  return foundation::Error{"storage.schema_migration_json_invalid", path + ": " + message};
}

foundation::Result<Json::Value> parseJson(const std::string& json) {
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  const std::unique_ptr<Json::CharReader> reader{builder.newCharReader()};
  if (!reader->parse(json.data(), json.data() + json.size(), &root, &errors)) {
    return parseError("$", "Invalid JSON. " + errors);
  }
  if (!root.isArray()) {
    return parseError("$", "Schema migration log must be a JSON array.");
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

foundation::Result<std::string> requiredStringMember(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isString()) {
    return parseError(path + "." + key, "Expected string.");
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

foundation::Result<std::int64_t> requiredInt64Member(const Json::Value& object, const char* key, const std::string& path) {
  auto value = requiredMember(object, key, path);
  if (!value) {
    return value.error();
  }
  if (!value.value().isIntegral()) {
    return parseError(path + "." + key, "Expected integer.");
  }
  return value.value().asInt64();
}

foundation::Result<SchemaMigrationRecord> parseRecord(const Json::Value& object, const std::string& path) {
  auto operationName = requiredStringMember(object, "operationName", path);
  if (!operationName) {
    return operationName.error();
  }
  auto fromSchemaVersion = requiredIntMember(object, "fromSchemaVersion", path);
  if (!fromSchemaVersion) {
    return fromSchemaVersion.error();
  }
  auto toSchemaVersion = requiredIntMember(object, "toSchemaVersion", path);
  if (!toSchemaVersion) {
    return toSchemaVersion.error();
  }
  auto appliedAtMs = requiredInt64Member(object, "appliedAtMs", path);
  if (!appliedAtMs) {
    return appliedAtMs.error();
  }

  return SchemaMigrationRecord{
    operationName.value(),
    fromSchemaVersion.value(),
    toSchemaVersion.value(),
    timePointFromMilliseconds(appliedAtMs.value())
  };
}

} // namespace

foundation::Result<void> SchemaMigrationLog::append(SchemaMigrationRecord record) {
  auto valid = validateRecord(record);
  if (!valid) {
    return valid.error();
  }
  records_.push_back(std::move(record));
  return {};
}

const std::vector<SchemaMigrationRecord>& SchemaMigrationLog::records() const noexcept {
  return records_;
}

std::string serializeCanonicalSchemaMigrationLog(const SchemaMigrationLog& log) {
  std::ostringstream stream;
  stream << '[';
  const std::vector<SchemaMigrationRecord>& records = log.records();
  for (std::size_t index = 0; index < records.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    const SchemaMigrationRecord& record = records[index];
    stream << '{';
    foundation::writeJsonStringProperty(stream, "operationName", record.operationName);
    stream << ",\"fromSchemaVersion\":" << record.fromSchemaVersion;
    stream << ",\"toSchemaVersion\":" << record.toSchemaVersion;
    stream << ",\"appliedAtMs\":" << millisecondsSinceEpoch(record.appliedAt);
    stream << '}';
  }
  stream << ']';
  return stream.str();
}

foundation::Result<SchemaMigrationLog> deserializeCanonicalSchemaMigrationLog(const std::string& json) {
  auto root = parseJson(json);
  if (!root) {
    return root.error();
  }

  SchemaMigrationLog log;
  for (Json::ArrayIndex index = 0; index < root.value().size(); ++index) {
    const std::string itemPath = "$.schemaMigrations[" + std::to_string(index) + "]";
    auto record = parseRecord(root.value()[index], itemPath);
    if (!record) {
      return record.error();
    }
    auto appended = log.append(record.value());
    if (!appended) {
      return parseError(itemPath, appended.error().message);
    }
  }
  return log;
}

} // namespace grapple::storage
