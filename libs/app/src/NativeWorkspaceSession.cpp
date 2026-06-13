#include <grapple/app/NativeWorkspaceSession.hpp>

#include <grapple/asset/Asset.hpp>
#include <grapple/media/CachingMediaReader.hpp>
#include <grapple/media/FrameCache.hpp>
#include <grapple/runtime/BuiltinEffectRuntime.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/storage/ProjectPackageReader.hpp>

#include <cstdlib>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

media::MediaSourceKind sourceKindFor(asset::AssetMediaType mediaType) {
  return mediaType == asset::AssetMediaType::Image
    ? media::MediaSourceKind::Image
    : media::MediaSourceKind::Video;
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

} // namespace

struct NativeWorkspaceSession::State {
  State(NativeProjectSession projectValue, media::MediaSourceCatalog mediaSourceCatalog)
    : project{std::move(projectValue)},
      commandWriter{project},
      effects{project, commandWriter},
      steward{project, commandWriter},
      mediaSources{std::move(mediaSourceCatalog)},
      mediaReader{mediaSources},
      frameCache{8},
      cachedMediaReader{mediaReader, frameCache},
      frameSource{cachedMediaReader},
      preview{project, frameSource, {&builtinEffectRuntime}},
      exportSession{project, {&builtinEffectRuntime}} {}

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
  return fromProject(std::move(project.value()));
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
  auto project = NativeProjectSession::openPackage(std::move(package));
  if (!project) {
    return project.error();
  }
  return replaceWithProject(std::move(project.value()));
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

        runtime::RuntimeEvaluator runtime{{&state_->builtinEffectRuntime}};
        auto prepared = runtime.prepare(runtime::PrepareRuntimePlanRequest{plan.value().plan});
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
