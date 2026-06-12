#include <grapple/project/ProjectSnapshot.hpp>

#include <grapple/project/ProjectSerializer.hpp>

namespace grapple::project {

ProjectSnapshot makeProjectSnapshot(const ProjectDocument& document) {
  ProjectSnapshot snapshot{
    document.info,
    document.revision,
    document.revisionNumber,
    document.settings,
    document.assets,
    document.graph,
    foundation::Hash256{}
  };
  snapshot.canonicalHash = hashProjectSnapshot(snapshot);
  return snapshot;
}

} // namespace grapple::project
