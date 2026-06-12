#pragma once

#include <grapple/foundation/Hash.hpp>
#include <grapple/project/ProjectCommand.hpp>
#include <grapple/project/ProjectDocument.hpp>
#include <grapple/project/ProjectEvents.hpp>
#include <grapple/project/ProjectSnapshot.hpp>

#include <string>

namespace grapple::project {

std::string serializeCanonicalProjectDocument(const ProjectDocument& document);
std::string serializeCanonicalProjectSnapshot(const ProjectSnapshot& snapshot);
foundation::Result<ProjectSnapshot> deserializeCanonicalProjectSnapshot(const std::string& json);
std::string serializeCanonicalCommandPayload(const ProjectCommand& command);
std::string serializeCanonicalEventPayload(const ProjectEvent& event);
foundation::Hash256 hashProjectSnapshot(const ProjectSnapshot& snapshot);

} // namespace grapple::project
