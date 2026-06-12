#include <grapple/render/ExportSession.hpp>

namespace grapple::render {

ExportSession::ExportSession(runtime::RuntimeEvaluator& runtime)
  : runtime_(runtime) {}

foundation::Result<void> ExportSession::start(const ExportRequest& request) {
  auto prepared = runtime_.prepare(runtime::PrepareRuntimePlanRequest{
    request.plan,
    runtime::RuntimePrepareMode::Export
  });
  if (!prepared) {
    return prepared.error();
  }

  state_.hasExport = true;
  state_.revision = request.plan.revision;
  state_.preparedPlanHash = prepared.value().prepared.planHash;
  state_.settings = request.settings;
  return {};
}

foundation::Result<ExportSessionState> ExportSession::state() const {
  return state_;
}

} // namespace grapple::render
