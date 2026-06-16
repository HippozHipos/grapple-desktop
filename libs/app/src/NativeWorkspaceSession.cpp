#include <grapple/app/NativeWorkspaceSession.hpp>

#include <grapple/app/MediaSourceMapping.hpp>
#include <grapple/app/NativeMediaImport.hpp>
#include <grapple/agent/AgentRunEventSerializer.hpp>
#include <grapple/agent/AgentRunSerializer.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/media/CachingMediaReader.hpp>
#include <grapple/media/FrameCache.hpp>
#include <grapple/project/ProjectController.hpp>
#include <grapple/project/ProjectSnapshot.hpp>
#include <grapple/render/LocalRenderSystem.hpp>
#include <grapple/runtime/BuiltinEffectRuntime.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/storage/ProjectPackageReader.hpp>
#include <grapple/storage/ProjectPackageSession.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

constexpr std::size_t MediaFrameCacheBytes = 256 * 1024 * 1024;

project::CommandSource importerSource() {
  return project::CommandSource{
    project::CommandSourceKind::Importer,
    std::nullopt,
    "workspace"
  };
}

foundation::Result<foundation::FilePath> packageMediaSourcePath(
  const storage::ProjectPackage& package,
  const foundation::FilePath& sourcePath
) {
  if (sourcePath.value.empty()) {
    return foundation::Error{"app.media_source_path_empty", "Media source path must not be empty."};
  }

  const std::filesystem::path path{sourcePath.value};
  if (path.is_absolute()) {
    return sourcePath;
  }
  if (package.rootPath.value.empty()) {
    return foundation::Error{"app.package_root_empty", "Workspace package root path must not be empty."};
  }
  return foundation::FilePath{(std::filesystem::path{package.rootPath.value} / path).lexically_normal().string()};
}

foundation::Result<media::MediaSourceCatalog> buildMediaSources(const NativeProjectSession& session) {
  auto snapshot = session.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  media::MediaSourceCatalog sources;
  const storage::ProjectPackage& package = session.packageState().package;
  for (const asset::Asset& asset : snapshot.value().assets.assets()) {
    auto mediaSourcePath = packageMediaSourcePath(package, asset.metadata.sourcePath);
    if (!mediaSourcePath) {
      return mediaSourcePath.error();
    }
    auto registered = sources.registerSource(media::MediaSource{
      asset.id,
      mediaSourceKindForAssetMediaType(asset.metadata.mediaType),
      mediaSourcePath.value()
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

foundation::Result<std::string> projectStemForPackageRoot(const foundation::FilePath& rootPath) {
  if (rootPath.value.empty()) {
    return foundation::Error{"app.package_root_empty", "Workspace package root path must not be empty."};
  }

  const std::filesystem::path path{rootPath.value};
  const std::string stem = path.filename().string();
  if (stem.empty() || stem == "." || stem == "..") {
    return foundation::Error{"app.package_project_stem_empty", "Package root must have a project directory name."};
  }

  std::string normalized;
  normalized.reserve(stem.size());
  for (const unsigned char character : stem) {
    if (std::isalnum(character) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(character)));
    } else {
      normalized.push_back('_');
    }
  }
  return normalized;
}

foundation::Result<storage::ProjectPackage> createWorkspacePackage(
  foundation::FilePath rootPath,
  const std::string& projectName
) {
  if (projectName.empty()) {
    return foundation::Error{"app.project_name_empty", "Workspace project name must not be empty."};
  }

  auto stem = projectStemForPackageRoot(rootPath);
  if (!stem) {
    return stem.error();
  }
  const std::filesystem::path root{rootPath.value};
  std::error_code rootExistsError;
  const bool rootExists = std::filesystem::exists(root, rootExistsError);
  if (rootExistsError) {
    return foundation::Error{"app.package_root_stat_failed", rootExistsError.message()};
  }
  if (rootExists) {
    std::error_code directoryError;
    if (!std::filesystem::is_directory(root, directoryError)) {
      return foundation::Error{"app.package_root_not_directory", "New package root must be a directory."};
    }
    if (directoryError) {
      return foundation::Error{"app.package_root_stat_failed", directoryError.message()};
    }
  }
  std::error_code existsError;
  const bool manifestExists = std::filesystem::exists(
    root / "manifest.json",
    existsError
  );
  if (existsError) {
    return foundation::Error{"app.package_manifest_stat_failed", existsError.message()};
  }
  if (manifestExists) {
    return foundation::Error{
      "app.package_manifest_already_exists",
      "New package root already contains a Grapple manifest. Open it instead."
    };
  }
  if (rootExists) {
    std::error_code iterateError;
    const std::filesystem::directory_iterator entry{root, iterateError};
    if (iterateError) {
      return foundation::Error{"app.package_root_iterate_failed", iterateError.message()};
    }
    if (entry != std::filesystem::directory_iterator{}) {
      return foundation::Error{
        "app.package_root_not_empty",
        "New package root must be empty. Choose an empty folder or create a new one."
      };
    }
  }

  return storage::ProjectPackage{
    foundation::ProjectId{"proj_" + stem.value()},
    std::move(rootPath),
    storage::CurrentProjectPackageSchemaVersion
  };
}

foundation::Result<foundation::FilePath> copyImportedMediaIntoPackage(
  const storage::ProjectPackage& package,
  const foundation::AssetId& assetId,
  const foundation::FilePath& sourcePath
) {
  if (package.rootPath.value.empty()) {
    return foundation::Error{"app.package_root_empty", "Workspace package root path must not be empty."};
  }

  const std::filesystem::path source{sourcePath.value};
  std::error_code sourceStatusError;
  if (!std::filesystem::is_regular_file(source, sourceStatusError)) {
    if (sourceStatusError) {
      return foundation::Error{"app.import_source_stat_failed", sourceStatusError.message()};
    }
    return foundation::Error{"app.import_source_missing", "Imported media source must be a regular file."};
  }

  const std::filesystem::path relativePath =
    std::filesystem::path{"assets"} / "originals" / (assetId.value() + source.extension().string());
  const std::filesystem::path destination = std::filesystem::path{package.rootPath.value} / relativePath;
  std::error_code directoryError;
  std::filesystem::create_directories(destination.parent_path(), directoryError);
  if (directoryError) {
    return foundation::Error{"app.import_media_directory_create_failed", directoryError.message()};
  }

  std::error_code copyError;
  std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, copyError);
  if (copyError) {
    return foundation::Error{"app.import_media_copy_failed", copyError.message()};
  }
  return foundation::FilePath{relativePath.generic_string()};
}

foundation::Result<foundation::FilePath> writeImportedMediaThumbnailIntoPackage(
  const storage::ProjectPackage& package,
  const foundation::AssetId& assetId,
  asset::AssetMediaType mediaType,
  const foundation::FilePath& mediaPath
) {
  if (mediaType == asset::AssetMediaType::Audio) {
    return foundation::Error{"app.thumbnail_media_type_invalid", "Imported thumbnails require image or video media."};
  }
  if (package.rootPath.value.empty()) {
    return foundation::Error{"app.package_root_empty", "Workspace package root path must not be empty."};
  }

  const std::filesystem::path relativePath =
    std::filesystem::path{"assets"} / "thumbnails" / (assetId.value() + ".jpg");
  const std::filesystem::path destination = std::filesystem::path{package.rootPath.value} / relativePath;
  std::error_code directoryError;
  std::filesystem::create_directories(destination.parent_path(), directoryError);
  if (directoryError) {
    return foundation::Error{"app.thumbnail_directory_create_failed", directoryError.message()};
  }

  auto written = writeNativeMediaThumbnail(
    mediaType,
    mediaPath,
    foundation::FilePath{destination.lexically_normal().string()}
  );
  if (!written) {
    return written.error();
  }
  return foundation::FilePath{relativePath.generic_string()};
}

foundation::Result<void> copyPackageRelativeFile(
  const storage::ProjectPackage& sourcePackage,
  const storage::ProjectPackage& destinationPackage,
  const foundation::FilePath& filePath,
  std::string missingCode,
  std::string missingMessage
) {
  if (sourcePackage.rootPath.value.empty() || destinationPackage.rootPath.value.empty()) {
    return foundation::Error{"app.package_root_empty", "Workspace package root path must not be empty."};
  }

  const std::filesystem::path relativePath{filePath.value};
  if (relativePath.empty() || relativePath.is_absolute()) {
    return {};
  }

  const std::filesystem::path sourcePath =
    (std::filesystem::path{sourcePackage.rootPath.value} / relativePath).lexically_normal();
  const std::filesystem::path destinationPath =
    (std::filesystem::path{destinationPackage.rootPath.value} / relativePath).lexically_normal();

  std::error_code equivalentError;
  if (std::filesystem::equivalent(sourcePath, destinationPath, equivalentError) && !equivalentError) {
    return {};
  }

  std::error_code sourceStatusError;
  if (!std::filesystem::is_regular_file(sourcePath, sourceStatusError)) {
    if (sourceStatusError) {
      return foundation::Error{std::move(missingCode), sourceStatusError.message()};
    }
    return foundation::Error{std::move(missingCode), std::move(missingMessage) + sourcePath.string() + "."};
  }

  std::error_code directoryError;
  std::filesystem::create_directories(destinationPath.parent_path(), directoryError);
  if (directoryError) {
    return foundation::Error{"app.package_media_directory_create_failed", directoryError.message()};
  }

  std::error_code copyError;
  std::filesystem::copy_file(
    sourcePath,
    destinationPath,
    std::filesystem::copy_options::overwrite_existing,
    copyError
  );
  if (copyError) {
    return foundation::Error{"app.package_media_copy_failed", copyError.message()};
  }

  return {};
}

foundation::Result<void> copyPackageLocalMediaFiles(
  const storage::ProjectPackage& sourcePackage,
  const storage::ProjectPackage& destinationPackage,
  const project::ProjectSnapshot& snapshot
) {
  for (const asset::Asset& asset : snapshot.assets.assets()) {
    auto sourceCopied = copyPackageRelativeFile(
      sourcePackage,
      destinationPackage,
      asset.metadata.sourcePath,
      "app.package_media_source_missing",
      "Package-local media source is missing: "
    );
    if (!sourceCopied) {
      return sourceCopied.error();
    }

    if (asset.metadata.thumbnailPath.has_value()) {
      auto thumbnailCopied = copyPackageRelativeFile(
        sourcePackage,
        destinationPackage,
        asset.metadata.thumbnailPath.value(),
        "app.package_media_thumbnail_missing",
        "Package-local media thumbnail is missing: "
      );
      if (!thumbnailCopied) {
        return thumbnailCopied.error();
      }
    }
  }

  return {};
}

foundation::Result<NativeProjectSession> createInitializedPackageProject(
  std::string projectName,
  storage::ProjectPackage package
) {
  const foundation::ProjectId projectId = package.projectId;
  project::ProjectDocument document = project::createEmptyProject(projectId, std::move(projectName));
  project::ProjectSnapshot snapshot = project::makeProjectSnapshot(document);

  storage::ProjectPackageState state;
  state.package = std::move(package);
  state.projectSnapshot = snapshot;
  state.snapshotDocuments.push_back(snapshot);

  const foundation::SnapshotId initialSnapshotId{"snap_initial_rev_0"};
  auto snapshotRecord = state.snapshots.append(history::SnapshotRecord{
    initialSnapshotId,
    snapshot.info.id,
    snapshot.revision,
    snapshot.canonicalHash,
    foundation::FilePath{"snapshots/rev_0.json"},
    std::optional<std::string>{"initial"},
    std::chrono::system_clock::now()
  });
  if (!snapshotRecord) {
    return snapshotRecord.error();
  }
  state.head = history::HistoryHead{
    snapshot.revision,
    std::nullopt,
    initialSnapshotId
  };

  return NativeProjectSession{storage::ProjectPackageSession{std::move(document), std::move(state)}};
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

foundation::Result<NativeWorkspaceWriteResult> writeWorkspacePackageSidecars(
  const storage::ProjectPackage& package,
  const NativePackageWriteResult& projectWrite,
  const NativeStewardSession& steward
) {
  auto runsPath = writePackageTextFile(
    package,
    agentRunsRelativePath(),
    agent::serializeCanonicalAgentRuns(steward.runs())
  );
  if (!runsPath) {
    return runsPath.error();
  }
  auto eventsPath = writePackageTextFile(
    package,
    agentEventsRelativePath(),
    agent::serializeCanonicalAgentRunEvents(steward.events())
  );
  if (!eventsPath) {
    return eventsPath.error();
  }

  return NativeWorkspaceWriteResult{
    projectWrite,
    runsPath.value(),
    eventsPath.value()
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
      frameCache{MediaFrameCacheBytes},
      cachedMediaReader{mediaReader, frameCache},
      frameSource{cachedMediaReader},
      runtime{{&builtinEffectRuntime}},
      renderCore{runtime, frameSource},
      renderSystem{renderCore},
      preview{project, renderSystem},
      exportSession{renderSystem} {}

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
  render::LocalRenderSystem renderSystem;
  NativePreviewSession preview;
  NativeExportSession exportSession;
  jobs::JobScheduler jobs;
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
  std::string projectName,
  storage::ProjectPackage package
) {
  auto project = createInitializedPackageProject(std::move(projectName), std::move(package));
  if (!project) {
    return project.error();
  }
  return fromProject(std::move(project.value()));
}

foundation::Result<NativeWorkspaceSession> NativeWorkspaceSession::createPackageRoot(
  foundation::FilePath rootPath,
  std::string projectName
) {
  auto package = createWorkspacePackage(std::move(rootPath), projectName);
  if (!package) {
    return package.error();
  }
  return create(std::move(projectName), std::move(package.value()));
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

foundation::Result<void> NativeWorkspaceSession::createPackageRootInPlace(
  foundation::FilePath rootPath,
  std::string projectName
) {
  auto workspace = createPackageRoot(std::move(rootPath), std::move(projectName));
  if (!workspace) {
    return workspace.error();
  }
  *this = std::move(workspace.value());
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

jobs::JobScheduler& NativeWorkspaceSession::jobs() noexcept {
  return state_->jobs;
}

media::MediaSourceCatalog& NativeWorkspaceSession::mediaSources() noexcept {
  return state_->mediaSources;
}

std::size_t NativeWorkspaceSession::cachedMediaFrameCount() const noexcept {
  return state_->frameCache.size();
}

foundation::Result<foundation::AssetId> NativeWorkspaceSession::importMediaFile(foundation::FilePath path) {
  const std::filesystem::path filesystemPath{path.value};
  auto inspectedAsset = inspectNativeMediaAsset(
    state_->commandWriter.nextAssetId(filesystemPath.stem().string()),
    path
  );
  if (!inspectedAsset) {
    return inspectedAsset.error();
  }

  const foundation::AssetId assetId = inspectedAsset.value().id;
  const asset::AssetMediaType mediaType = inspectedAsset.value().metadata.mediaType;
  auto packageRelativePath = copyImportedMediaIntoPackage(
    state_->project.packageState().package,
    assetId,
    path
  );
  if (!packageRelativePath) {
    return packageRelativePath.error();
  }
  inspectedAsset.value().metadata.sourcePath = packageRelativePath.value();
  if (mediaType != asset::AssetMediaType::Audio) {
    auto thumbnailPath = writeImportedMediaThumbnailIntoPackage(
      state_->project.packageState().package,
      assetId,
      mediaType,
      path
    );
    if (!thumbnailPath) {
      return thumbnailPath.error();
    }
    inspectedAsset.value().metadata.thumbnailPath = thumbnailPath.value();
  }
  auto mediaSourcePath = packageMediaSourcePath(
    state_->project.packageState().package,
    inspectedAsset.value().metadata.sourcePath
  );
  if (!mediaSourcePath) {
    return mediaSourcePath.error();
  }

  auto registeredAsset = state_->commandWriter.apply(
    project::RegisterAssetCommand{std::move(inspectedAsset.value())},
    importerSource()
  );
  if (!registeredAsset) {
    return registeredAsset.error();
  }

  auto registeredSource = state_->mediaSources.registerSource(media::MediaSource{
    assetId,
    mediaSourceKindForAssetMediaType(mediaType),
    mediaSourcePath.value()
  });
  if (!registeredSource) {
    return registeredSource.error();
  }

  return assetId;
}

foundation::Result<NativeWorkspaceWriteResult> NativeWorkspaceSession::writePackage() const {
  auto projectWrite = state_->project.writePackage();
  if (!projectWrite) {
    return projectWrite.error();
  }

  const storage::ProjectPackage& package = state_->project.packageState().package;
  return writeWorkspacePackageSidecars(package, projectWrite.value(), state_->steward);
}

foundation::Result<NativeWorkspaceWriteResult> NativeWorkspaceSession::savePackageAs(foundation::FilePath rootPath) {
  const storage::ProjectPackage& currentPackage = state_->project.packageState().package;
  storage::ProjectPackage package{
    currentPackage.projectId,
    std::move(rootPath),
    currentPackage.schemaVersion
  };

  auto snapshot = state_->project.snapshot();
  if (!snapshot) {
    return snapshot.error();
  }

  auto mediaCopy = copyPackageLocalMediaFiles(currentPackage, package, snapshot.value());
  if (!mediaCopy) {
    return mediaCopy.error();
  }

  auto projectWrite = state_->project.writePackageTo(package);
  if (!projectWrite) {
    return projectWrite.error();
  }

  auto sidecarsWrite = writeWorkspacePackageSidecars(package, projectWrite.value(), state_->steward);
  if (!sidecarsWrite) {
    return sidecarsWrite.error();
  }

  auto retargeted = state_->project.retargetPackage(std::move(package));
  if (!retargeted) {
    return retargeted.error();
  }
  return sidecarsWrite.value();
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
