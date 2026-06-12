#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <string>

namespace grapple::storage {

struct ProjectPackage {
  foundation::ProjectId projectId;
  foundation::FilePath rootPath;
  int schemaVersion = 1;
};

} // namespace grapple::storage

