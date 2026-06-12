#include <grapple/project/ProjectSerializer.hpp>

#include <grapple/foundation/Json.hpp>
#include <grapple/graph/GraphSerializer.hpp>

#include <sstream>

namespace grapple::project {

std::string serializeCanonicalProjectDocument(const ProjectDocument& document) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "projectId", document.info.id.value());
  stream << ',';
  foundation::writeJsonStringProperty(stream, "name", document.info.name);
  stream << ',';
  foundation::writeJsonStringProperty(stream, "revision", document.revision.value());
  stream << ",\"revisionNumber\":" << document.revisionNumber;
  stream << ",\"graph\":" << graph::serializeCanonicalGraph(document.graph);
  stream << '}';
  return stream.str();
}

foundation::Hash256 hashProjectSnapshot(const ProjectSnapshot& snapshot) {
  return foundation::stableHash(serializeCanonicalProjectDocument(snapshot.document));
}

} // namespace grapple::project
