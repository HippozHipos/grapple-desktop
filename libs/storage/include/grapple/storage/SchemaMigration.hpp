#pragma once

#include <grapple/foundation/Result.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace grapple::storage {

struct SchemaMigrationRecord {
  std::string operationName;
  int fromSchemaVersion = 0;
  int toSchemaVersion = 0;
  std::chrono::system_clock::time_point appliedAt;
};

class SchemaMigrationLog {
public:
  foundation::Result<void> append(SchemaMigrationRecord record);

  [[nodiscard]] const std::vector<SchemaMigrationRecord>& records() const noexcept;

private:
  std::vector<SchemaMigrationRecord> records_;
};

std::string serializeCanonicalSchemaMigrationLog(const SchemaMigrationLog& log);
foundation::Result<SchemaMigrationLog> deserializeCanonicalSchemaMigrationLog(const std::string& json);

} // namespace grapple::storage
