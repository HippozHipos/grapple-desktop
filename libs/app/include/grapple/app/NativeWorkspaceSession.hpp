#pragma once

#include <grapple/app/NativeEffectSession.hpp>
#include <grapple/app/NativeExportSession.hpp>
#include <grapple/app/NativeMediaFrameSource.hpp>
#include <grapple/app/NativePreviewSession.hpp>
#include <grapple/app/NativeProjectCommandWriter.hpp>
#include <grapple/app/NativeProjectSession.hpp>
#include <grapple/app/NativeStewardSession.hpp>
#include <grapple/media/MediaSource.hpp>
#include <grapple/media/OpenCVMediaReader.hpp>

#include <memory>
#include <string>

namespace grapple::app {

struct NativeWorkspaceWriteResult {
  NativePackageWriteResult project;
  foundation::FilePath agentRunsPath;
  foundation::FilePath agentEventsPath;
};

class NativeWorkspaceSession final : public project::IProjectQueryService {
public:
  NativeWorkspaceSession(NativeWorkspaceSession&&) noexcept;
  NativeWorkspaceSession& operator=(NativeWorkspaceSession&&) noexcept;
  NativeWorkspaceSession(const NativeWorkspaceSession&) = delete;
  NativeWorkspaceSession& operator=(const NativeWorkspaceSession&) = delete;
  ~NativeWorkspaceSession();

  static foundation::Result<NativeWorkspaceSession> fromProject(NativeProjectSession project);
  static foundation::Result<NativeWorkspaceSession> create(
    foundation::ProjectId projectId,
    std::string projectName,
    storage::ProjectPackage package
  );
  static foundation::Result<NativeWorkspaceSession> openPackage(storage::ProjectPackage package);
  static foundation::Result<NativeWorkspaceSession> openPackageRoot(foundation::FilePath rootPath);

  foundation::Result<void> replaceWithProject(NativeProjectSession project);
  foundation::Result<void> openPackageInPlace(storage::ProjectPackage package);
  foundation::Result<void> openPackageRootInPlace(foundation::FilePath rootPath);
  [[nodiscard]] foundation::Result<NativeWorkspaceWriteResult> writePackage() const;

  [[nodiscard]] NativeProjectSession& project() noexcept;
  [[nodiscard]] const NativeProjectSession& project() const noexcept;
  [[nodiscard]] NativeProjectCommandWriter& commandWriter() noexcept;
  [[nodiscard]] NativeEffectSession& effects() noexcept;
  [[nodiscard]] NativeStewardSession& steward() noexcept;
  [[nodiscard]] NativePreviewSession& preview() noexcept;
  [[nodiscard]] NativeExportSession& exportSession() noexcept;
  [[nodiscard]] media::MediaSourceCatalog& mediaSources() noexcept;
  [[nodiscard]] std::size_t cachedMediaFrameCount() const noexcept;

  [[nodiscard]] foundation::Result<project::ProjectQueryResult> query(
    const project::ProjectQuery& query
  ) const override;

private:
  NativeWorkspaceSession() = default;

  struct State;
  std::unique_ptr<State> state_;
};

} // namespace grapple::app
