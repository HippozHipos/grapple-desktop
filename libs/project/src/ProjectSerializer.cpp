#include <grapple/project/ProjectSerializer.hpp>

#include <grapple/graph/GraphSerializer.hpp>

#include <sstream>

namespace grapple::project {

namespace {

std::string escapeString(const std::string& value) {
  std::ostringstream stream;
  for (const char character : value) {
    if (character == '\\') {
      stream << "\\\\";
    } else if (character == '"') {
      stream << "\\\"";
    } else if (character == '\n') {
      stream << "\\n";
    } else {
      stream << character;
    }
  }
  return stream.str();
}

void writeKeyValue(std::ostringstream& stream, const std::string& key, const std::string& value) {
  stream << '"' << key << "\":\"" << escapeString(value) << '"';
}

} // namespace

std::string serializeCanonicalProjectDocument(const ProjectDocument& document) {
  std::ostringstream stream;
  stream << '{';
  writeKeyValue(stream, "projectId", document.info.id.value());
  stream << ',';
  writeKeyValue(stream, "name", document.info.name);
  stream << ',';
  writeKeyValue(stream, "revision", document.revision.value());
  stream << ",\"revisionNumber\":" << document.revisionNumber;
  stream << ",\"graph\":" << graph::serializeCanonicalGraph(document.graph);
  stream << '}';
  return stream.str();
}

foundation::Hash256 hashProjectSnapshot(const ProjectSnapshot& snapshot) {
  return foundation::stableHash(serializeCanonicalProjectDocument(snapshot.document));
}

} // namespace grapple::project

