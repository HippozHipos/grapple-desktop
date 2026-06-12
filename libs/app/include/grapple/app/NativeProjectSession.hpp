#pragma once

#include <grapple/app/AppViewModel.hpp>
#include <grapple/foundation/Result.hpp>
#include <grapple/project/ProjectDocument.hpp>
#include <grapple/project/ProjectQuery.hpp>
#include <grapple/projection/ProjectionQueryService.hpp>
#include <grapple/storage/ProjectPackageSession.hpp>

#include <string>

namespace grapple::app {

struct NativePackageWriteResult {
  foundation::FilePath snapshotPath;
  foundation::FilePath manifestPath;
  foundation::FilePath commandLogPath;
  foundation::FilePath eventLogPath;
};

class NativeProjectSession final : public project::IProjectQueryService {
public:
  NativeProjectSession(foundation::ProjectId projectId, std::string projectName, storage::ProjectPackage package);
  NativeProjectSession(project::ProjectDocument document, storage::ProjectPackage package);

  foundation::Result<storage::ProjectPackageSessionResult> applyAndCommit(
    const project::ProjectCommandEnvelope& command,
    storage::ProjectCommitRecordOptions options
  );

  [[nodiscard]] foundation::Result<project::ProjectSnapshot> snapshot() const;
  [[nodiscard]] foundation::Result<project::ProjectQueryResult> query(const project::ProjectQuery& query) const override;
  [[nodiscard]] foundation::Result<AppViewModel> buildViewModel() const;
  [[nodiscard]] foundation::Result<projection::BuildTimelineIRResult> buildTimelineIR() const;
  [[nodiscard]] foundation::Result<projection::BuildRenderPlanResult> buildRenderPlan() const;
  [[nodiscard]] foundation::Result<NativePackageWriteResult> writePackage() const;
  [[nodiscard]] const storage::ProjectPackageState& packageState() const noexcept;

private:
  storage::ProjectPackageSession session_;
};

} // namespace grapple::app
