#include <grapple/app/NativeWorkspaceSession.hpp>

#include <grapple/app/MediaSourceMapping.hpp>
#include <grapple/agent/AgentRunEventSerializer.hpp>
#include <grapple/agent/AgentRunSerializer.hpp>
#include <grapple/asset/Asset.hpp>
#include <grapple/media/CachingMediaReader.hpp>
#include <grapple/media/FrameCache.hpp>
#include <grapple/runtime/BuiltinEffectRuntime.hpp>
#include <grapple/runtime/RuntimeEvaluator.hpp>
#include <grapple/storage/ProjectPackageReader.hpp>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <variant>

namespace grapple::app {

namespace {

constexpr std::size_t MediaFrameCacheBytes = 256 * 1024 * 1024;

project::CommandSource importerSource() {
  return project::CommandSource{
    project::CommandSourceKind::Importer,
    std::nullopt,
    "desktop"
  };
}

std::optional<std::uint32_t> readLittleEndianU32(std::istream& input) {
  unsigned char bytes[4]{};
  input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!input) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::optional<std::uint16_t> readLittleEndianU16(std::istream& input) {
  unsigned char bytes[2]{};
  input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
  if (!input) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(bytes[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::optional<foundation::TimeSeconds> readWavDuration(const foundation::FilePath& path) {
  std::ifstream input{path.value, std::ios::binary};
  if (!input) {
    return std::nullopt;
  }

  char riff[4]{};
  char wave[4]{};
  input.read(riff, sizeof(riff));
  const auto riffSize = readLittleEndianU32(input);
  input.read(wave, sizeof(wave));
  if (!riffSize.has_value() || std::string_view{riff, 4} != "RIFF" || std::string_view{wave, 4} != "WAVE") {
    return std::nullopt;
  }

  std::optional<std::uint16_t> channels;
  std::optional<std::uint32_t> sampleRate;
  std::optional<std::uint16_t> bitsPerSample;
  std::optional<std::uint32_t> dataBytes;

  while (input) {
    char chunkId[4]{};
    input.read(chunkId, sizeof(chunkId));
    if (!input) {
      break;
    }
    const auto chunkSize = readLittleEndianU32(input);
    if (!chunkSize.has_value()) {
      return std::nullopt;
    }

    const std::string_view chunk{chunkId, 4};
    if (chunk == "fmt " && chunkSize.value() >= 16U) {
      const auto audioFormat = readLittleEndianU16(input);
      channels = readLittleEndianU16(input);
      sampleRate = readLittleEndianU32(input);
      input.seekg(6, std::ios::cur);
      bitsPerSample = readLittleEndianU16(input);
      if (!audioFormat.has_value() || audioFormat.value() != 1U || !channels.has_value() || !sampleRate.has_value() || !bitsPerSample.has_value()) {
        return std::nullopt;
      }
      const std::uint32_t remaining = chunkSize.value() - 16U;
      if (remaining > 0U) {
        input.seekg(static_cast<std::streamoff>(remaining), std::ios::cur);
      }
    } else if (chunk == "data") {
      dataBytes = chunkSize.value();
      input.seekg(static_cast<std::streamoff>(chunkSize.value()), std::ios::cur);
    } else {
      input.seekg(static_cast<std::streamoff>(chunkSize.value()), std::ios::cur);
    }

    if ((chunkSize.value() % 2U) == 1U) {
      input.seekg(1, std::ios::cur);
    }
  }

  if (!channels.has_value() || !sampleRate.has_value() || !bitsPerSample.has_value() || !dataBytes.has_value()) {
    return std::nullopt;
  }
  const std::uint32_t bytesPerSampleFrame = static_cast<std::uint32_t>(channels.value()) *
                                            (static_cast<std::uint32_t>(bitsPerSample.value()) / 8U);
  if (bytesPerSampleFrame == 0U || sampleRate.value() == 0U) {
    return std::nullopt;
  }
  return foundation::TimeSeconds{
    static_cast<double>(dataBytes.value()) /
      static_cast<double>(bytesPerSampleFrame * sampleRate.value())
  };
}

std::string lowerExtension(const foundation::FilePath& path) {
  std::string extension = std::filesystem::path{path.value}.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return extension;
}

foundation::Result<asset::AssetMediaType> mediaTypeForPath(const foundation::FilePath& path) {
  const std::string extension = lowerExtension(path);
  if (extension == ".mov" || extension == ".mp4" || extension == ".avi" || extension == ".mkv") {
    return asset::AssetMediaType::Video;
  }
  if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".ppm" || extension == ".webp") {
    return asset::AssetMediaType::Image;
  }
  if (extension == ".wav" || extension == ".aiff" || extension == ".aif" || extension == ".mp3" || extension == ".flac") {
    return asset::AssetMediaType::Audio;
  }
  return foundation::Error{"app.media_type_unsupported", "Unsupported media file extension for " + path.value + "."};
}

foundation::Result<asset::Asset> inspectVideoAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
) {
  cv::VideoCapture capture{path.value};
  if (!capture.isOpened()) {
    return foundation::Error{"app.video_open_failed", "Could not inspect video file " + path.value + "."};
  }

  const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
  const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
  const double frameCount = capture.get(cv::CAP_PROP_FRAME_COUNT);
  const double framesPerSecond = capture.get(cv::CAP_PROP_FPS);
  if (width <= 0 || height <= 0 || frameCount <= 0.0 || framesPerSecond <= 0.0) {
    return foundation::Error{"app.video_metadata_invalid", "Video file metadata is incomplete for " + path.value + "."};
  }

  const std::filesystem::path filesystemPath{path.value};
  return asset::Asset{
    assetId,
    filesystemPath.stem().string(),
    asset::AssetMetadata{
      asset::AssetMediaType::Video,
      path,
      std::nullopt,
      foundation::TimeSeconds{frameCount / framesPerSecond},
      foundation::Resolution{width, height},
      foundation::FrameRate{static_cast<std::int32_t>(framesPerSecond * 1000.0), 1000}
    }
  };
}

foundation::Result<asset::Asset> inspectImageAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
) {
  const cv::Mat decoded = cv::imread(path.value, cv::IMREAD_UNCHANGED);
  if (decoded.empty()) {
    return foundation::Error{"app.image_open_failed", "Could not inspect image file " + path.value + "."};
  }

  const std::filesystem::path filesystemPath{path.value};
  return asset::Asset{
    assetId,
    filesystemPath.stem().string(),
    asset::AssetMetadata{
      asset::AssetMediaType::Image,
      path,
      std::nullopt,
      std::nullopt,
      foundation::Resolution{decoded.cols, decoded.rows},
      std::nullopt
    }
  };
}

foundation::Result<asset::Asset> inspectAudioAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
) {
  if (!std::filesystem::is_regular_file(std::filesystem::path{path.value})) {
    return foundation::Error{"app.audio_file_missing", "Audio file does not exist: " + path.value + "."};
  }

  const std::filesystem::path filesystemPath{path.value};
  return asset::Asset{
    assetId,
    filesystemPath.stem().string(),
    asset::AssetMetadata{
      asset::AssetMediaType::Audio,
      path,
      std::nullopt,
      readWavDuration(path),
      std::nullopt,
      std::nullopt
    }
  };
}

foundation::Result<asset::Asset> inspectMediaAsset(
  const foundation::AssetId& assetId,
  const foundation::FilePath& path
) {
  auto mediaType = mediaTypeForPath(path);
  if (!mediaType) {
    return mediaType.error();
  }

  switch (mediaType.value()) {
    case asset::AssetMediaType::Video:
      return inspectVideoAsset(assetId, path);
    case asset::AssetMediaType::Audio:
      return inspectAudioAsset(assetId, path);
    case asset::AssetMediaType::Image:
      return inspectImageAsset(assetId, path);
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
      mediaSourceKindForAssetMediaType(asset.metadata.mediaType),
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

foundation::Result<foundation::AssetId> NativeWorkspaceSession::importMediaFile(foundation::FilePath path) {
  const std::filesystem::path filesystemPath{path.value};
  auto inspectedAsset = inspectMediaAsset(
    state_->commandWriter.nextAssetId(filesystemPath.stem().string()),
    path
  );
  if (!inspectedAsset) {
    return inspectedAsset.error();
  }

  const foundation::AssetId assetId = inspectedAsset.value().id;
  const asset::AssetMediaType mediaType = inspectedAsset.value().metadata.mediaType;
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
    std::move(path)
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
