#pragma once

#include <grapple/agent/AgentConversationState.hpp>
#include <grapple/app/AppViewModel.hpp>
#include <grapple/foundation/StrongId.hpp>

#include <QWidget>

#include <functional>
#include <optional>
#include <string>

class QPushButton;
class QListWidget;
class QTextEdit;

namespace grapple::ui {

class StewardPanel final : public QWidget {
public:
  using ImportMediaHandler = std::function<void()>;
  using AddCameraHandler = std::function<void()>;
  using AddSelectedMediaHandler = std::function<void()>;
  using ShowCameraControlsHandler = std::function<void(foundation::NodeId)>;
  using CreateCameraEffectHandler = std::function<void(std::string)>;
  using AdjustCameraControlsHandler = std::function<void(foundation::NodeId, std::string)>;
  using EditSelectedClipHandler = std::function<void(foundation::NodeId, std::string)>;
  using TryEditSelectedClipHandler = std::function<bool(foundation::NodeId, std::string)>;
  using SelectEditTargetHandler = std::function<void(foundation::NodeId)>;

  explicit StewardPanel(QWidget* parent = nullptr);

  void setImportMediaHandler(ImportMediaHandler handler);
  void setAddCameraHandler(AddCameraHandler handler);
  void setAddSelectedMediaHandler(AddSelectedMediaHandler handler);
  void setShowCameraControlsHandler(ShowCameraControlsHandler handler);
  void setCreateCameraEffectHandler(CreateCameraEffectHandler handler);
  void setAdjustCameraControlsHandler(AdjustCameraControlsHandler handler);
  void setEditSelectedClipHandler(EditSelectedClipHandler handler);
  void setTryEditSelectedClipHandler(TryEditSelectedClipHandler handler);
  void setSelectEditTargetHandler(SelectEditTargetHandler handler);
  void setViewModel(
    const app::AppViewModel& viewModel,
    const agent::AgentConversationState& conversationState,
    const std::optional<foundation::NodeId>& selectedNodeId,
    const std::optional<foundation::AssetId>& selectedAssetId
  );
  void setIntent(std::string intent);
  void triggerPrimaryAction();
  [[nodiscard]] std::string contents() const;
  [[nodiscard]] std::string intent() const;
  [[nodiscard]] std::string intentPlaceholder() const;
  [[nodiscard]] std::string primaryActionText() const;
  [[nodiscard]] bool primaryActionEnabled() const;
  void triggerSelectedClipAction();
  [[nodiscard]] std::string selectedClipActionText() const;
  [[nodiscard]] bool selectedClipActionEnabled() const;
  void triggerRecentEdit(int row);
  [[nodiscard]] int recentEditCount() const;
  [[nodiscard]] int currentRecentEditRow() const;
  [[nodiscard]] std::string recentEditText(int row) const;

private:
  enum class PrimaryAction {
    Disabled,
    ImportMedia,
    AddSelectedMedia,
    AddCamera,
    AdjustCameraControls,
    ShowCameraControls,
    CreateCameraEffect
  };

  void updateActionButtons();
  void updateActionLabels();
  void updateIntentPlaceholder();
  [[nodiscard]] bool tryEditSelectedClipFromPrimaryAction();
  [[nodiscard]] bool intentHasText() const;
  [[nodiscard]] bool primaryActionCanRun() const;
  [[nodiscard]] bool selectedClipActionCanRun() const;

  ImportMediaHandler importMediaHandler_;
  AddCameraHandler addCameraHandler_;
  AddSelectedMediaHandler addSelectedMediaHandler_;
  ShowCameraControlsHandler showCameraControlsHandler_;
  CreateCameraEffectHandler createCameraEffectHandler_;
  AdjustCameraControlsHandler adjustCameraControlsHandler_;
  EditSelectedClipHandler editSelectedClipHandler_;
  TryEditSelectedClipHandler tryEditSelectedClipHandler_;
  SelectEditTargetHandler selectEditTargetHandler_;
  PrimaryAction primaryAction_ = PrimaryAction::Disabled;
  std::optional<foundation::NodeId> primaryTargetCameraNodeId_;
  std::optional<foundation::NodeId> selectedClipTargetNodeId_;
  QTextEdit* intent_ = nullptr;
  QPushButton* primaryActionButton_ = nullptr;
  QPushButton* selectedClipActionButton_ = nullptr;
  QListWidget* recentEdits_ = nullptr;
  QTextEdit* text_ = nullptr;
};

} // namespace grapple::ui
