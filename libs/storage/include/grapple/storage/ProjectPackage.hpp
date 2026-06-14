#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <string>

namespace grapple::storage {

inline constexpr int CurrentProjectPackageSchemaVersion = 2;

struct ProjectPackage {
  foundation::ProjectId projectId;
  foundation::FilePath rootPath;
  int schemaVersion;
};

} // namespace grapple::storage
