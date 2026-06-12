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

ProjectDocument makeProjectDocument(const ProjectSnapshot& snapshot) {
  return ProjectDocument{
    snapshot.info,
    snapshot.revision,
    snapshot.revisionNumber,
    snapshot.settings,
    snapshot.assets,
    snapshot.graph
  };
}

} // namespace grapple::project
