#include <grapple/app/NativeWorkspaceSession.hpp>

#include <grapple/asset/Asset.hpp>
#include <grapple/runtime/BuiltinEffectRuntime.hpp>
#include <grapple/storage/ProjectPackageReader.hpp>

#include <utility>

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

} // namespace

struct NativeWorkspaceSession::State {
  State(NativeProjectSession projectValue, media::MediaSourceCatalog mediaSourceCatalog)
    : project{std::move(projectValue)},
      commandWriter{project},
      effects{project, commandWriter},
      steward{project, commandWriter},
      mediaSources{std::move(mediaSourceCatalog)},
      mediaReader{mediaSources},
      frameSource{mediaReader},
      preview{project, frameSource, {&builtinEffectRuntime}},
      exportSession{project, {&builtinEffectRuntime}} {}

  NativeProjectSession project;
  NativeProjectCommandWriter commandWriter;
  NativeEffectSession effects;
  NativeStewardSession steward;
  media::MediaSourceCatalog mediaSources;
  media::OpenCVMediaReader mediaReader;
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

} // namespace grapple::app
