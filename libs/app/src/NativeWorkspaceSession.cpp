#include <grapple/app/NativeWorkspaceSession.hpp>

#include <grapple/agent/AgentRunEventSerializer.hpp>
#include <grapple/agent/AgentRunSerializer.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/media/CachingMediaReader.hpp>
#include <grapple/media/FrameCache.hpp>
#include <grapple/runtime/BuiltinEffectRuntime.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/storage/ProjectPackageReader.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

constexpr std::size_t MediaFrameCacheBytes = 256 * 1024 * 1024;

media::MediaSourceKind sourceKindFor(asset::AssetMediaType mediaType) {
  switch (mediaType) {
    case asset::AssetMediaType::Video:
      return media::MediaSourceKind::Video;
    case asset::AssetMediaType::Audio:
      return media::MediaSourceKind::Audio;
    case asset::AssetMediaType::Image:
      return media::MediaSourceKind::Image;
  }

  std::abort();
}

foundation::Result<media::MediaSourceCatalog> buildMediaSources(const NativeProjectSession& session) {
  auto snapshot = session.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  media::MediaSourceCatalog sources;
  for (const asset::Asset& asset : snapshot.value().assets.assets()) {
    auto registered = sources.registerSource(media::MediaSource{
      asset.id,
      sourceKindFor(asset.metadata.mediaType),
      asset.metadata.sourcePath
    });
    if (!registered) {
      return registered.error();
    }
  }

  return sources;
}

project::RuntimeDiagnosticSeveritySummary runtimeDiagnosticSeverity(
  runtime::DiagnosticSeverity severity
) {
  switch (severity) {
    case runtime::DiagnosticSeverity::Info:
      return project::RuntimeDiagnosticSeveritySummary::Info;
    case runtime::DiagnosticSeverity::Warning:
      return project::RuntimeDiagnosticSeveritySummary::Warning;
    case runtime::DiagnosticSeverity::Error:
      return project::RuntimeDiagnosticSeveritySummary::Error;
  }

  std::abort();
}

project::RuntimeDiagnosticSummary runtimeDiagnosticSummary(
  const runtime::RuntimeDiagnostic& diagnostic
) {
  return project::RuntimeDiagnosticSummary{
    diagnostic.code,
    runtimeDiagnosticSeverity(diagnostic.severity),
    diagnostic.location.projectId,
    diagnostic.location.revision,
    diagnostic.location.nodeId,
    diagnostic.message
  };
}

foundation::FilePath agentRunsRelativePath() {
  return foundation::FilePath{"agent/runs.json"};
}

foundation::FilePath agentEventsRelativePath() {
  return foundation::FilePath{"agent/events.json"};
}

foundation::Result<std::filesystem::path> packagePath(
  const storage::ProjectPackage& package,
  const foundation::FilePath& relativePath
) {
  if (package.rootPath.value.empty()) {
    return foundation::Error{"app.package_root_empty", "Workspace package root path must not be empty."};
  }
  if (relativePath.value.empty()) {
    return foundation::Error{"app.package_sidecar_path_empty", "Workspace sidecar path must not be empty."};
  }
  const std::filesystem::path path{relativePath.value};
  if (path.is_absolute()) {
    return foundation::Error{"app.package_sidecar_path_absolute", "Workspace sidecar path must be package-relative."};
  }
  return std::filesystem::path{package.rootPath.value} / path;
}

foundation::Result<foundation::FilePath> writePackageTextFile(
  const storage::ProjectPackage& package,
  const foundation::FilePath& relativePath,
  const std::string& contents
) {
  auto path = packagePath(package, relativePath);
  if (!path) {
    return path.error();
  }

  const std::filesystem::path parent = path.value().parent_path();
  if (!parent.empty()) {
    std::error_code directoryError;
    std::filesystem::create_directories(parent, directoryError);
    if (directoryError) {
      return foundation::Error{"app.package_sidecar_directory_create_failed", directoryError.message()};
    }
  }

  std::ofstream output{path.value(), std::ios::binary | std::ios::trunc};
  if (!output) {
    return foundation::Error{"app.package_sidecar_open_failed", "Could not open workspace sidecar for writing."};
  }
  output << contents;
  if (!output) {
    return foundation::Error{"app.package_sidecar_write_failed", "Could not write workspace sidecar."};
  }
  return foundation::FilePath{path.value().lexically_normal().string()};
}

foundation::Result<std::string> readPackageTextFile(
  const storage::ProjectPackage& package,
  const foundation::FilePath& relativePath
) {
  auto path = packagePath(package, relativePath);
  if (!path) {
    return path.error();
  }

  std::ifstream input{path.value(), std::ios::binary};
  if (!input) {
    return foundation::Error{"app.package_sidecar_open_failed", "Could not open workspace sidecar for reading."};
  }

  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return foundation::Error{"app.package_sidecar_read_failed", "Could not read workspace sidecar."};
  }
  return contents.str();
}

foundation::Result<bool> packageTextFileExists(
  const storage::ProjectPackage& package,
  const foundation::FilePath& relativePath
) {
  auto path = packagePath(package, relativePath);
  if (!path) {
    return path.error();
  }

  std::error_code existsError;
  const bool exists = std::filesystem::exists(path.value(), existsError);
  if (existsError) {
    return foundation::Error{"app.package_sidecar_stat_failed", existsError.message()};
  }
  return exists;
}

foundation::Result<void> restoreAgentConversationSidecar(
  NativeWorkspaceSession& workspace,
  const storage::ProjectPackage& package
) {
  auto runsExist = packageTextFileExists(package, agentRunsRelativePath());
  if (!runsExist) {
    return runsExist.error();
  }
  auto eventsExist = packageTextFileExists(package, agentEventsRelativePath());
  if (!eventsExist) {
    return eventsExist.error();
  }

  if (!runsExist.value() && !eventsExist.value()) {
    return {};
  }
  if (runsExist.value() != eventsExist.value()) {
    return foundation::Error{
      "app.package_agent_sidecar_incomplete",
      "Workspace agent sidecar must contain both runs and events."
    };
  }

  auto runsContents = readPackageTextFile(package, agentRunsRelativePath());
  if (!runsContents) {
    return runsContents.error();
  }
  auto eventsContents = readPackageTextFile(package, agentEventsRelativePath());
  if (!eventsContents) {
    return eventsContents.error();
  }
  auto runs = agent::deserializeCanonicalAgentRuns(runsContents.value());
  if (!runs) {
    return runs.error();
  }
  auto events = agent::deserializeCanonicalAgentRunEvents(eventsContents.value());
  if (!events) {
    return events.error();
  }
  return workspace.steward().restoreConversation(std::move(runs.value()), std::move(events.value()));
}

} // namespace

struct NativeWorkspaceSession::State {
  State(NativeProjectSession projectValue, media::MediaSourceCatalog mediaSourceCatalog)
    : project{std::move(projectValue)},
      commandWriter{project},
      effects{project, commandWriter},
      steward{project, commandWriter},
      mediaSources{std::move(mediaSourceCatalog)},
      mediaReader{mediaSources},
      frameCache{MediaFrameCacheBytes},
      cachedMediaReader{mediaReader, frameCache},
      frameSource{cachedMediaReader},
      runtime{{&builtinEffectRuntime}},
      renderCore{runtime, frameSource},
      preview{project, renderCore},
      exportSession{project, renderCore} {}

  NativeProjectSession project;
  NativeProjectCommandWriter commandWriter;
  NativeEffectSession effects;
  NativeStewardSession steward;
  media::MediaSourceCatalog mediaSources;
  media::OpenCVMediaReader mediaReader;
  media::FrameCache frameCache;
  media::CachingMediaReader cachedMediaReader;
  NativeMediaFrameSource frameSource;
  runtime::BuiltinEffectRuntime builtinEffectRuntime;
  runtime::RuntimeEvaluator runtime;
  render::LocalRenderCore renderCore;
  NativePreviewSession preview;
  NativeExportSession exportSession;
};

NativeWorkspaceSession::NativeWorkspaceSession(NativeWorkspaceSession&&) noexcept = default;

NativeWorkspaceSession& NativeWorkspaceSession::operator=(NativeWorkspaceSession&&) noexcept = default;

NativeWorkspaceSession::~NativeWorkspaceSession() = default;

foundation::Result<NativeWorkspaceSession> NativeWorkspaceSession::fromProject(NativeProjectSession project) {
  NativeWorkspaceSession workspace;
  auto replaced = workspace.replaceWithProject(std::move(project));
  if (!replaced) {
    return replaced.error();
  }
  return std::move(workspace);
}

foundation::Result<NativeWorkspaceSession> NativeWorkspaceSession::create(
  foundation::ProjectId projectId,
  std::string projectName,
  storage::ProjectPackage package
) {
  return fromProject(NativeProjectSession{
    std::move(projectId),
    std::move(projectName),
    std::move(package)
  });
}

foundation::Result<NativeWorkspaceSession> NativeWorkspaceSession::openPackage(storage::ProjectPackage package) {
  auto project = NativeProjectSession::openPackage(std::move(package));
  if (!project) {
    return project.error();
  }
  auto workspace = fromProject(std::move(project.value()));
  if (!workspace) {
    return workspace.error();
  }

  const storage::ProjectPackage& openedPackage = workspace.value().state_->project.packageState().package;
  auto restored = restoreAgentConversationSidecar(workspace.value(), openedPackage);
  if (!restored) {
    return restored.error();
  }
  return std::move(workspace.value());
}

foundation::Result<NativeWorkspaceSession> NativeWorkspaceSession::openPackageRoot(foundation::FilePath rootPath) {
  const storage::ProjectPackageReader reader;
  auto package = reader.readPackage(std::move(rootPath));
  if (!package) {
    return package.error();
  }
  return openPackage(std::move(package.value()));
}

foundation::Result<void> NativeWorkspaceSession::replaceWithProject(NativeProjectSession project) {
  auto mediaSources = buildMediaSources(project);
  if (!mediaSources) {
    return mediaSources.error();
  }

  state_ = std::make_unique<State>(std::move(project), std::move(mediaSources.value()));
  return {};
}

foundation::Result<void> NativeWorkspaceSession::openPackageInPlace(storage::ProjectPackage package) {
  auto workspace = openPackage(std::move(package));
  if (!workspace) {
    return workspace.error();
  }
  *this = std::move(workspace.value());
  return {};
}

foundation::Result<void> NativeWorkspaceSession::openPackageRootInPlace(foundation::FilePath rootPath) {
  const storage::ProjectPackageReader reader;
  auto package = reader.readPackage(std::move(rootPath));
  if (!package) {
    return package.error();
  }
  return openPackageInPlace(std::move(package.value()));
}

NativeProjectSession& NativeWorkspaceSession::project() noexcept {
  return state_->project;
}

const NativeProjectSession& NativeWorkspaceSession::project() const noexcept {
  return state_->project;
}

NativeProjectCommandWriter& NativeWorkspaceSession::commandWriter() noexcept {
  return state_->commandWriter;
}

NativeEffectSession& NativeWorkspaceSession::effects() noexcept {
  return state_->effects;
}

NativeStewardSession& NativeWorkspaceSession::steward() noexcept {
  return state_->steward;
}

NativePreviewSession& NativeWorkspaceSession::preview() noexcept {
  return state_->preview;
}

NativeExportSession& NativeWorkspaceSession::exportSession() noexcept {
  return state_->exportSession;
}

media::MediaSourceCatalog& NativeWorkspaceSession::mediaSources() noexcept {
  return state_->mediaSources;
}

std::size_t NativeWorkspaceSession::cachedMediaFrameCount() const noexcept {
  return state_->frameCache.size();
}

foundation::Result<NativeWorkspaceWriteResult> NativeWorkspaceSession::writePackage() const {
  auto projectWrite = state_->project.writePackage();
  if (!projectWrite) {
    return projectWrite.error();
  }

  const storage::ProjectPackage& package = state_->project.packageState().package;
  auto runsPath = writePackageTextFile(
    package,
    agentRunsRelativePath(),
    agent::serializeCanonicalAgentRuns(state_->steward.runs())
  );
  if (!runsPath) {
    return runsPath.error();
  }
  auto eventsPath = writePackageTextFile(
    package,
    agentEventsRelativePath(),
    agent::serializeCanonicalAgentRunEvents(state_->steward.events())
  );
  if (!eventsPath) {
    return eventsPath.error();
  }

  return NativeWorkspaceWriteResult{
    projectWrite.value(),
    runsPath.value(),
    eventsPath.value()
  };
}

foundation::Result<project::ProjectQueryResult> NativeWorkspaceSession::query(
  const project::ProjectQuery& query
) const {
  return std::visit(
    [&](const auto& typedQuery) -> foundation::Result<project::ProjectQueryResult> {
      using Query = std::decay_t<decltype(typedQuery)>;
      if constexpr (std::is_same_v<Query, project::InspectRuntimeDiagnosticsQuery>) {
        auto plan = state_->project.buildRenderPlan();
        if (!plan) {
          return plan.error();
        }

        auto prepared = state_->runtime.prepare(runtime::PrepareRuntimePlanRequest{plan.value().plan});
        if (!prepared) {
          return prepared.error();
        }

        project::RuntimeInspectDiagnosticsResult result{
          plan.value().plan.revision,
          {}
        };
        for (const runtime::RuntimeDiagnostic& diagnostic : prepared.value().diagnostics) {
          result.diagnostics.push_back(runtimeDiagnosticSummary(diagnostic));
        }
        return project::ProjectQueryResult{std::move(result)};
      } else {
        return state_->project.query(typedQuery);
      }
    },
    query
  );
}

} // namespace grapple::app
