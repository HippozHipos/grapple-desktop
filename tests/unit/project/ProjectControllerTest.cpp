#include <grapple/project/ProjectController.hpp>

#include <TestAssert.hpp>

namespace {

grapple::project::ProjectCommandEnvelope makeCreateComposition(
  grapple::foundation::RevisionId expectedRevision
) {
  return grapple::project::ProjectCommandEnvelope{
    grapple::foundation::CommandId{"cmd_create_composition"},
    grapple::project::CommandKind::CreateComposition,
    grapple::foundation::ProjectId{"proj_test"},
    std::move(expectedRevision),
    grapple::project::CommandSource{grapple::project::CommandSourceKind::User, std::nullopt, "test"},
    grapple::project::CreateCompositionCommand{
      grapple::foundation::NodeId{"node_composition"},
      "Main"
    }
  };
}

} // namespace

int main() {
  using namespace grapple;

  project::ProjectController controller{
    project::createEmptyProject(foundation::ProjectId{"proj_test"}, "Test Project")
  };

  const auto initialSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(initialSnapshot);
  GRAPPLE_REQUIRE(initialSnapshot.value().document.revision == foundation::RevisionId{"rev_0"});
  GRAPPLE_REQUIRE(initialSnapshot.value().document.graph.nodes().empty());

  const auto createComposition = controller.apply(
    makeCreateComposition(initialSnapshot.value().document.revision)
  );
  GRAPPLE_REQUIRE(createComposition);
  GRAPPLE_REQUIRE(createComposition.value().beforeRevision == foundation::RevisionId{"rev_0"});
  GRAPPLE_REQUIRE(createComposition.value().afterRevision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(createComposition.value().events.size() == 2);

  const auto afterComposition = controller.snapshot();
  GRAPPLE_REQUIRE(afterComposition);
  GRAPPLE_REQUIRE(afterComposition.value().document.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(afterComposition.value().document.graph.nodes().size() == 1);

  const auto staleCommand = controller.apply(
    makeCreateComposition(foundation::RevisionId{"rev_0"})
  );
  GRAPPLE_REQUIRE(!staleCommand);
  GRAPPLE_REQUIRE(staleCommand.error().code == "project.expected_revision_mismatch");

  const auto afterStale = controller.snapshot();
  GRAPPLE_REQUIRE(afterStale);
  GRAPPLE_REQUIRE(afterStale.value().document.revision == foundation::RevisionId{"rev_1"});
  GRAPPLE_REQUIRE(afterStale.value().document.graph.nodes().size() == 1);

  const project::ProjectCommandEnvelope createTrack{
    foundation::CommandId{"cmd_create_track"},
    project::CommandKind::CreateTrack,
    foundation::ProjectId{"proj_test"},
    afterStale.value().document.revision,
    project::CommandSource{project::CommandSourceKind::User, std::nullopt, "test"},
    project::CreateTrackCommand{
      foundation::NodeId{"node_track"},
      foundation::NodeId{"node_composition"},
      foundation::EdgeId{"edge_contains_track"},
      "Video"
    }
  };

  const auto trackResult = controller.apply(createTrack);
  GRAPPLE_REQUIRE(trackResult);
  GRAPPLE_REQUIRE(trackResult.value().afterRevision == foundation::RevisionId{"rev_2"});

  const auto finalSnapshot = controller.snapshot();
  GRAPPLE_REQUIRE(finalSnapshot);
  GRAPPLE_REQUIRE(finalSnapshot.value().document.graph.nodes().size() == 2);
  GRAPPLE_REQUIRE(finalSnapshot.value().document.graph.edges().size() == 1);

  const auto graphQuery = controller.query(project::GetGraphQuery{});
  GRAPPLE_REQUIRE(graphQuery);
  const auto* graphResult = std::get_if<project::GraphResult>(&graphQuery.value());
  GRAPPLE_REQUIRE(graphResult != nullptr);
  GRAPPLE_REQUIRE(graphResult->graph.nodes().size() == 2);
  GRAPPLE_REQUIRE(graphResult->graph.edges().size() == 1);

  const auto snapshotQuery = controller.query(project::GetProjectSnapshotQuery{});
  GRAPPLE_REQUIRE(snapshotQuery);
  const auto* snapshotResult = std::get_if<project::ProjectSnapshotResult>(&snapshotQuery.value());
  GRAPPLE_REQUIRE(snapshotResult != nullptr);
  GRAPPLE_REQUIRE(snapshotResult->snapshot.document.revision == foundation::RevisionId{"rev_2"});

  return 0;
}
