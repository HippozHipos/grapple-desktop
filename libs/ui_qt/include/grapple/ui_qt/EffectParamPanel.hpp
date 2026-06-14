#pragma once

#include <grapple/app/AppViewModel.hpp>
#include <grapple/foundation/Time.hpp>

#include <QWidget>

#include <functional>
#include <optional>
#include <string>

class QVBoxLayout;

namespace grapple::ui {

class EffectParamPanel final : public QWidget {
public:
  using ApplyHandler = std::function<void(foundation::NodeId, std::string, timeline::ParamValue)>;
  using DeleteHandler = std::function<void(foundation::NodeId)>;
  using SetKeyframeHandler = std::function<void(foundation::NodeId, std::string, timeline::ParamValue, std::optional<foundation::KeyframeId>)>;
  using DeleteKeyframeHandler = std::function<void(foundation::NodeId, std::string, foundation::KeyframeId)>;

  explicit EffectParamPanel(QWidget* parent = nullptr);

  void setApplyHandler(ApplyHandler handler);
  void setDeleteHandler(DeleteHandler handler);
  void setKeyframeHandler(SetKeyframeHandler handler);
  void setDeleteKeyframeHandler(DeleteKeyframeHandler handler);
  void setSelection(
    const app::AppViewModel& viewModel,
    const std::optional<foundation::NodeId>& selectedNodeId,
    foundation::TimeSeconds playhead
  );

private:
  void clearControls();
  void addMessage(const QString& message);

  QVBoxLayout* layout_ = nullptr;
  ApplyHandler applyHandler_;
  DeleteHandler deleteHandler_;
  SetKeyframeHandler setKeyframeHandler_;
  DeleteKeyframeHandler deleteKeyframeHandler_;
};

} // namespace grapple::ui
