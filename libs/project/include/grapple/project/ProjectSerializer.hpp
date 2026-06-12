#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/project/ProjectDocument.hpp>

#include <string>

namespace grapple::project {

std::string serializeCanonicalProjectDocument(const ProjectDocument& document);
foundation::Hash256 hashProjectSnapshot(const ProjectSnapshot& snapshot);

} // namespace grapple::project

