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
  using TryDeleteSelectedClipHandler = std::function<bool(foundation::NodeId, std::string)>;
  using TryEditSelectedClipHandler = std::function<bool(foundation::NodeId, std::string)>;
  using EditSelectedTextClipHandler = std::function<void(foundation::NodeId, std::string)>;
  using TryEditSelectedTextClipHandler = std::function<bool(foundation::NodeId, std::string)>;
  using TryCreateTextClipHandler = std::function<bool(std::string)>;
  using EditSelectedNoteHandler = std::function<void(foundation::NodeId, std::string)>;
  using TryEditSelectedNoteHandler = std::function<bool(foundation::NodeId, std::string)>;
  using TryCreateNoteHandler = std::function<bool(std::string)>;
  using SelectEditTargetHandler = std::function<void(foundation::NodeId)>;

  explicit StewardPanel(QWidget* parent = nullptr);

  void setImportMediaHandler(ImportMediaHandler handler);
  void setAddCameraHandler(AddCameraHandler handler);
  void setAddSelectedMediaHandler(AddSelectedMediaHandler handler);
  void setShowCameraControlsHandler(ShowCameraControlsHandler handler);
  void setCreateCameraEffectHandler(CreateCameraEffectHandler handler);
  void setAdjustCameraControlsHandler(AdjustCameraControlsHandler handler);
  void setEditSelectedClipHandler(EditSelectedClipHandler handler);
  void setTryDeleteSelectedClipHandler(TryDeleteSelectedClipHandler handler);
  void setTryEditSelectedClipHandler(TryEditSelectedClipHandler handler);
  void setEditSelectedTextClipHandler(EditSelectedTextClipHandler handler);
  void setTryEditSelectedTextClipHandler(TryEditSelectedTextClipHandler handler);
  void setTryCreateTextClipHandler(TryCreateTextClipHandler handler);
  void setEditSelectedNoteHandler(EditSelectedNoteHandler handler);
  void setTryEditSelectedNoteHandler(TryEditSelectedNoteHandler handler);
  void setTryCreateNoteHandler(TryCreateNoteHandler handler);
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
  void triggerSelectedTargetAction();
  [[nodiscard]] std::string selectedTargetActionText() const;
  [[nodiscard]] bool selectedTargetActionEnabled() const;
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
  [[nodiscard]] bool tryDeleteSelectedClipFromPrimaryAction();
  [[nodiscard]] bool tryEditSelectedClipFromPrimaryAction();
  [[nodiscard]] bool tryEditSelectedTextClipFromPrimaryAction();
  [[nodiscard]] bool tryEditSelectedNoteFromPrimaryAction();
  [[nodiscard]] bool tryCreateTextClipFromPrimaryAction();
  [[nodiscard]] bool tryCreateNoteFromPrimaryAction();
  [[nodiscard]] bool intentHasText() const;
  [[nodiscard]] bool primaryActionCanRun() const;
  [[nodiscard]] bool selectedTargetActionCanRun() const;

  ImportMediaHandler importMediaHandler_;
  AddCameraHandler addCameraHandler_;
  AddSelectedMediaHandler addSelectedMediaHandler_;
  ShowCameraControlsHandler showCameraControlsHandler_;
  CreateCameraEffectHandler createCameraEffectHandler_;
  AdjustCameraControlsHandler adjustCameraControlsHandler_;
  EditSelectedClipHandler editSelectedClipHandler_;
  TryDeleteSelectedClipHandler tryDeleteSelectedClipHandler_;
  TryEditSelectedClipHandler tryEditSelectedClipHandler_;
  EditSelectedTextClipHandler editSelectedTextClipHandler_;
  TryEditSelectedTextClipHandler tryEditSelectedTextClipHandler_;
  TryCreateTextClipHandler tryCreateTextClipHandler_;
  EditSelectedNoteHandler editSelectedNoteHandler_;
  TryEditSelectedNoteHandler tryEditSelectedNoteHandler_;
  TryCreateNoteHandler tryCreateNoteHandler_;
  SelectEditTargetHandler selectEditTargetHandler_;
  PrimaryAction primaryAction_ = PrimaryAction::Disabled;
  std::optional<foundation::NodeId> primaryTargetCameraNodeId_;
  std::optional<foundation::NodeId> selectedClipTargetNodeId_;
  std::optional<foundation::NodeId> selectedTextClipTargetNodeId_;
  std::optional<foundation::NodeId> selectedNoteTargetNodeId_;
  QTextEdit* intent_ = nullptr;
  QPushButton* primaryActionButton_ = nullptr;
  QPushButton* selectedTargetActionButton_ = nullptr;
  QListWidget* recentEdits_ = nullptr;
  QTextEdit* text_ = nullptr;
};

} // namespace grapple::ui
