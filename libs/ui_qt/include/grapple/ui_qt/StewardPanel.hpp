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
  enum class SelectedTargetKind {
    Clip,
    TextClip,
    Track,
    Note
  };

  using ImportMediaHandler = std::function<void()>;
  using AddCameraHandler = std::function<void()>;
  using AddSelectedMediaHandler = std::function<void()>;
  using ShowCameraControlsHandler = std::function<void(foundation::NodeId)>;
  using CreateCameraEffectHandler = std::function<void(std::string)>;
  using HistoryIntentTargetsEditHandler = std::function<bool(std::string)>;
  using TryApplyHistoryIntentHandler = std::function<bool(std::string)>;
  using CameraUpdateIntentTargetsCameraHandler = std::function<bool(std::string)>;
  using TryUpdateCameraHandler = std::function<bool(foundation::NodeId, std::string)>;
  using TryDeleteCameraControlsHandler = std::function<bool(foundation::NodeId, std::string)>;
  using AdjustCameraControlsHandler = std::function<void(foundation::NodeId, std::string)>;
  using EditSelectedClipHandler = std::function<void(foundation::NodeId, std::string)>;
  using SelectedTargetIntentTargetsSelectionHandler = std::function<bool(SelectedTargetKind, std::string)>;
  using TryDeleteSelectedClipHandler = std::function<bool(foundation::NodeId, std::string)>;
  using TryDeleteSelectedTrackHandler = std::function<bool(foundation::NodeId, std::string)>;
  using TryCreateClipTintHandler = std::function<bool(foundation::NodeId, std::string)>;
  using TryEditSelectedClipHandler = std::function<bool(foundation::NodeId, std::string)>;
  using EditSelectedTextClipHandler = std::function<void(foundation::NodeId, std::string)>;
  using TextClipIntentTargetsTextHandler = std::function<bool(std::string)>;
  using TryEditSelectedTextClipHandler = std::function<bool(foundation::NodeId, std::string)>;
  using TrackCreateIntentTargetsTrackHandler = std::function<bool(std::string)>;
  using TryCreateTrackHandler = std::function<bool(std::string)>;
  using TryCreateTextClipHandler = std::function<bool(std::string)>;
  using EditSelectedNoteHandler = std::function<void(foundation::NodeId, std::string)>;
  using NoteIntentTargetsNoteHandler = std::function<bool(std::string)>;
  using TryEditSelectedNoteHandler = std::function<bool(foundation::NodeId, std::string)>;
  using TryCreateNoteHandler = std::function<bool(std::string)>;
  using SelectEditTargetHandler = std::function<void(foundation::NodeId)>;

  explicit StewardPanel(QWidget* parent = nullptr);

  void setImportMediaHandler(ImportMediaHandler handler);
  void setAddCameraHandler(AddCameraHandler handler);
  void setAddSelectedMediaHandler(AddSelectedMediaHandler handler);
  void setShowCameraControlsHandler(ShowCameraControlsHandler handler);
  void setCreateCameraEffectHandler(CreateCameraEffectHandler handler);
  void setHistoryIntentTargetsEditHandler(HistoryIntentTargetsEditHandler handler);
  void setTryApplyHistoryIntentHandler(TryApplyHistoryIntentHandler handler);
  void setCameraUpdateIntentTargetsCameraHandler(CameraUpdateIntentTargetsCameraHandler handler);
  void setTryUpdateCameraHandler(TryUpdateCameraHandler handler);
  void setTryDeleteCameraControlsHandler(TryDeleteCameraControlsHandler handler);
  void setAdjustCameraControlsHandler(AdjustCameraControlsHandler handler);
  void setEditSelectedClipHandler(EditSelectedClipHandler handler);
  void setSelectedTargetIntentTargetsSelectionHandler(SelectedTargetIntentTargetsSelectionHandler handler);
  void setTryDeleteSelectedClipHandler(TryDeleteSelectedClipHandler handler);
  void setTryDeleteSelectedTrackHandler(TryDeleteSelectedTrackHandler handler);
  void setTryCreateClipTintHandler(TryCreateClipTintHandler handler);
  void setTryEditSelectedClipHandler(TryEditSelectedClipHandler handler);
  void setEditSelectedTextClipHandler(EditSelectedTextClipHandler handler);
  void setTextClipIntentTargetsTextHandler(TextClipIntentTargetsTextHandler handler);
  void setTryEditSelectedTextClipHandler(TryEditSelectedTextClipHandler handler);
  void setTrackCreateIntentTargetsTrackHandler(TrackCreateIntentTargetsTrackHandler handler);
  void setTryCreateTrackHandler(TryCreateTrackHandler handler);
  void setTryCreateTextClipHandler(TryCreateTextClipHandler handler);
  void setEditSelectedNoteHandler(EditSelectedNoteHandler handler);
  void setNoteIntentTargetsNoteHandler(NoteIntentTargetsNoteHandler handler);
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
  [[nodiscard]] bool tryApplyHistoryIntentFromPrimaryAction();
  [[nodiscard]] bool tryUpdateCameraFromPrimaryAction();
  [[nodiscard]] bool tryDeleteCameraControlsFromPrimaryAction();
  [[nodiscard]] bool tryDeleteSelectedClipFromPrimaryAction();
  [[nodiscard]] bool tryDeleteSelectedTrackFromPrimaryAction();
  [[nodiscard]] bool tryCreateClipTintFromPrimaryAction();
  [[nodiscard]] bool tryEditSelectedClipFromPrimaryAction();
  [[nodiscard]] bool tryEditSelectedTextClipFromPrimaryAction();
  [[nodiscard]] bool tryEditSelectedNoteFromPrimaryAction();
  [[nodiscard]] bool tryCreateTrackFromPrimaryAction();
  [[nodiscard]] bool tryCreateTextClipFromPrimaryAction();
  [[nodiscard]] bool tryCreateNoteFromPrimaryAction();
  [[nodiscard]] bool intentHasText() const;
  [[nodiscard]] bool selectedTargetIntentTargetsSelection() const;
  [[nodiscard]] bool primaryActionCanRun() const;
  [[nodiscard]] bool selectedTargetActionCanRun() const;

  ImportMediaHandler importMediaHandler_;
  AddCameraHandler addCameraHandler_;
  AddSelectedMediaHandler addSelectedMediaHandler_;
  ShowCameraControlsHandler showCameraControlsHandler_;
  CreateCameraEffectHandler createCameraEffectHandler_;
  HistoryIntentTargetsEditHandler historyIntentTargetsEditHandler_;
  TryApplyHistoryIntentHandler tryApplyHistoryIntentHandler_;
  CameraUpdateIntentTargetsCameraHandler cameraUpdateIntentTargetsCameraHandler_;
  TryUpdateCameraHandler tryUpdateCameraHandler_;
  TryDeleteCameraControlsHandler tryDeleteCameraControlsHandler_;
  AdjustCameraControlsHandler adjustCameraControlsHandler_;
  EditSelectedClipHandler editSelectedClipHandler_;
  SelectedTargetIntentTargetsSelectionHandler selectedTargetIntentTargetsSelectionHandler_;
  TryDeleteSelectedClipHandler tryDeleteSelectedClipHandler_;
  TryDeleteSelectedTrackHandler tryDeleteSelectedTrackHandler_;
  TryCreateClipTintHandler tryCreateClipTintHandler_;
  TryEditSelectedClipHandler tryEditSelectedClipHandler_;
  EditSelectedTextClipHandler editSelectedTextClipHandler_;
  TextClipIntentTargetsTextHandler textClipIntentTargetsTextHandler_;
  TryEditSelectedTextClipHandler tryEditSelectedTextClipHandler_;
  TrackCreateIntentTargetsTrackHandler trackCreateIntentTargetsTrackHandler_;
  TryCreateTrackHandler tryCreateTrackHandler_;
  TryCreateTextClipHandler tryCreateTextClipHandler_;
  EditSelectedNoteHandler editSelectedNoteHandler_;
  NoteIntentTargetsNoteHandler noteIntentTargetsNoteHandler_;
  TryEditSelectedNoteHandler tryEditSelectedNoteHandler_;
  TryCreateNoteHandler tryCreateNoteHandler_;
  SelectEditTargetHandler selectEditTargetHandler_;
  PrimaryAction primaryAction_ = PrimaryAction::Disabled;
  std::optional<foundation::NodeId> primaryTargetCameraNodeId_;
  std::optional<foundation::NodeId> selectedClipTargetNodeId_;
  std::optional<foundation::NodeId> selectedTextClipTargetNodeId_;
  std::optional<foundation::NodeId> selectedTrackTargetNodeId_;
  std::optional<foundation::NodeId> selectedNoteTargetNodeId_;
  QTextEdit* intent_ = nullptr;
  QPushButton* primaryActionButton_ = nullptr;
  QPushButton* selectedTargetActionButton_ = nullptr;
  QListWidget* recentEdits_ = nullptr;
  QTextEdit* text_ = nullptr;
};

} // namespace grapple::ui
