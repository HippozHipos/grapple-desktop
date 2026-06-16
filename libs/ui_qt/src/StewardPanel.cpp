#include <grapple/ui_qt/StewardPanel.hpp>

#include <QKeySequence>
#include <QListWidget>
#include <QPushButton>
#include <QShortcut>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <utility>

namespace grapple::ui {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

QString runStatusText(agent::AgentRunStatus status) {
  switch (status) {
    case agent::AgentRunStatus::Pending:
      return "pending";
    case agent::AgentRunStatus::Running:
      return "running";
    case agent::AgentRunStatus::Succeeded:
      return "succeeded";
    case agent::AgentRunStatus::Failed:
      return "failed";
  }

  return "unknown";
}

QString toolStatusText(agent::AgentConversationToolCallStatus status) {
  switch (status) {
    case agent::AgentConversationToolCallStatus::Running:
      return "running";
    case agent::AgentConversationToolCallStatus::Succeeded:
      return "succeeded";
    case agent::AgentConversationToolCallStatus::Failed:
      return "failed";
  }

  return "unknown";
}

QString diagnosticSeverityText(agent::DiagnosticSeverity severity) {
  switch (severity) {
    case agent::DiagnosticSeverity::Info:
      return "info";
    case agent::DiagnosticSeverity::Warning:
      return "warning";
    case agent::DiagnosticSeverity::Error:
      return "error";
  }

  return "unknown";
}

std::optional<foundation::NodeId> selectedVisualClipNodeId(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  if (!selectedNodeId.has_value()) {
    return std::nullopt;
  }

  const auto selectedClip = std::find_if(
    viewModel.timeline.clips.begin(),
    viewModel.timeline.clips.end(),
    [&](const app::AppClipRow& clip) {
      return clip.sourceNodeId == selectedNodeId.value();
    }
  );
  if (selectedClip == viewModel.timeline.clips.end()) {
    return std::nullopt;
  }
  return selectedClip->sourceNodeId;
}

std::optional<foundation::NodeId> selectedTextClipNodeId(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  if (!selectedNodeId.has_value()) {
    return std::nullopt;
  }

  const auto selectedClip = std::find_if(
    viewModel.timeline.textClips.begin(),
    viewModel.timeline.textClips.end(),
    [&](const app::AppTextClipRow& clip) {
      return clip.sourceNodeId == selectedNodeId.value();
    }
  );
  if (selectedClip == viewModel.timeline.textClips.end()) {
    return std::nullopt;
  }
  return selectedClip->sourceNodeId;
}

std::optional<foundation::NodeId> selectedNoteNodeId(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  if (!selectedNodeId.has_value()) {
    return std::nullopt;
  }

  const auto selectedNote = std::find_if(
    viewModel.notes.rows.begin(),
    viewModel.notes.rows.end(),
    [&](const app::AppNoteRow& note) {
      return note.sourceNodeId == selectedNodeId.value();
    }
  );
  if (selectedNote == viewModel.notes.rows.end()) {
    return std::nullopt;
  }
  return selectedNote->sourceNodeId;
}

QString cameraName(
  const app::AppViewModel& viewModel,
  const foundation::NodeId& cameraNodeId
) {
  for (const app::AppCameraRow& camera : viewModel.timeline.cameras) {
    if (camera.sourceNodeId == cameraNodeId) {
      return qString(camera.name);
    }
  }
  return qString(cameraNodeId.value());
}

QString clipName(
  const app::AppViewModel& viewModel,
  const foundation::NodeId& clipNodeId
) {
  for (const app::AppClipRow& clip : viewModel.timeline.clips) {
    if (clip.sourceNodeId == clipNodeId) {
      return qString(clip.assetName);
    }
  }
  return qString(clipNodeId.value());
}

QString textClipName(
  const app::AppViewModel& viewModel,
  const foundation::NodeId& clipNodeId
) {
  for (const app::AppTextClipRow& clip : viewModel.timeline.textClips) {
    if (clip.sourceNodeId == clipNodeId) {
      return qString(clip.text);
    }
  }
  return qString(clipNodeId.value());
}

QString noteName(
  const app::AppViewModel& viewModel,
  const foundation::NodeId& noteNodeId
) {
  for (const app::AppNoteRow& note : viewModel.notes.rows) {
    if (note.sourceNodeId == noteNodeId) {
      return qString(note.title);
    }
  }
  return qString(noteNodeId.value());
}

QString assetName(
  const app::AppViewModel& viewModel,
  const foundation::AssetId& assetId
) {
  for (const app::AppAssetRow& asset : viewModel.assets.rows) {
    if (asset.assetId == assetId) {
      return qString(asset.name);
    }
  }
  return qString(assetId.value());
}

bool containsNonWhitespace(const std::string& value) {
  return std::any_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isspace(character) == 0;
  });
}

QString stewardEditName(const app::AppStewardEditRow& edit) {
  return edit.editName.empty() ? QString{"Edit"} : qString(edit.editName);
}

QString stewardEditRequest(const app::AppStewardEditRow& edit) {
  return edit.intent.empty() ? stewardEditName(edit) : qString(edit.intent);
}

QString stewardEditTargetSuffix(const app::AppStewardEditRow& edit) {
  return edit.targetName.empty() ? QString{} : QString{" on %1"}.arg(qString(edit.targetName));
}

QString stewardEditResultLabel(const app::AppStewardEditRow& edit) {
  if (edit.targetName.empty() && edit.editName.empty()) {
    return {};
  }
  return QString{"%1%2"}.arg(stewardEditName(edit)).arg(stewardEditTargetSuffix(edit));
}

QString stewardEditResult(const app::AppStewardEditRow& edit) {
  const QString resultLabel = stewardEditResultLabel(edit);
  if (resultLabel.isEmpty()) {
    return {};
  }
  return QString{" -> %1"}.arg(resultLabel);
}

QString stewardEditControlSummary(const app::AppStewardEditRow& edit) {
  return edit.controlSummary.empty() ? QString{} : qString(edit.controlSummary);
}

const app::AppStewardEditRow* latestTargetedEdit(const app::AppViewModel& viewModel) {
  for (auto edit = viewModel.steward.edits.rbegin(); edit != viewModel.steward.edits.rend(); ++edit) {
    if (edit->targetNodeId.has_value()) {
      return &(*edit);
    }
  }
  return nullptr;
}

} // namespace

StewardPanel::StewardPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("stewardPanel");

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);

  intent_ = new QTextEdit;
  intent_->setObjectName("stewardIntent");
  intent_->setPlaceholderText("Describe the edit you want Steward to apply. Ctrl+Enter applies it.");
  intent_->setAcceptRichText(false);
  intent_->setLineWrapMode(QTextEdit::WidgetWidth);
  intent_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  intent_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  intent_->setMinimumHeight(48);
  intent_->setMaximumHeight(64);
  layout->addWidget(intent_);
  connect(intent_, &QTextEdit::textChanged, this, [this] {
    updateActionLabels();
    updateActionButtons();
  });

  primaryActionButton_ = new QPushButton{"Create Editable Camera Controls"};
  primaryActionButton_->setObjectName("stewardPrimaryAction");
  layout->addWidget(primaryActionButton_);
  connect(primaryActionButton_, &QPushButton::clicked, this, [this] {
    if (tryEditSelectedClipFromPrimaryAction()) {
      return;
    }
    if (tryEditSelectedTextClipFromPrimaryAction()) {
      return;
    }
    if (tryEditSelectedNoteFromPrimaryAction()) {
      return;
    }
    if (tryCreateTextClipFromPrimaryAction()) {
      return;
    }
    if (tryCreateNoteFromPrimaryAction()) {
      return;
    }

    switch (primaryAction_) {
      case PrimaryAction::AddCamera:
        if (addCameraHandler_) {
          addCameraHandler_();
        }
        return;
      case PrimaryAction::ImportMedia:
        if (importMediaHandler_) {
          importMediaHandler_();
        }
        return;
      case PrimaryAction::AddSelectedMedia:
        if (addSelectedMediaHandler_) {
          addSelectedMediaHandler_();
        }
        return;
      case PrimaryAction::ShowCameraControls:
        if (showCameraControlsHandler_ && primaryTargetCameraNodeId_.has_value()) {
          showCameraControlsHandler_(primaryTargetCameraNodeId_.value());
        }
        return;
      case PrimaryAction::CreateCameraEffect:
        if (createCameraEffectHandler_) {
          createCameraEffectHandler_(intent());
        }
        return;
      case PrimaryAction::AdjustCameraControls:
        if (adjustCameraControlsHandler_ && primaryTargetCameraNodeId_.has_value()) {
          adjustCameraControlsHandler_(primaryTargetCameraNodeId_.value(), intent());
        }
        return;
      case PrimaryAction::Disabled:
        return;
    }
  });

  auto* submitShortcut = new QShortcut{QKeySequence{QStringLiteral("Ctrl+Return")}, this};
  submitShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(submitShortcut, &QShortcut::activated, this, [this] {
    triggerPrimaryAction();
  });
  auto* keypadSubmitShortcut = new QShortcut{QKeySequence{QStringLiteral("Ctrl+Enter")}, this};
  keypadSubmitShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(keypadSubmitShortcut, &QShortcut::activated, this, [this] {
    triggerPrimaryAction();
  });

  selectedTargetActionButton_ = new QPushButton{"Apply Request To Clip"};
  selectedTargetActionButton_->setObjectName("stewardSelectedTargetAction");
  layout->addWidget(selectedTargetActionButton_);
  connect(selectedTargetActionButton_, &QPushButton::clicked, this, [this] {
    if (editSelectedClipHandler_ && selectedClipTargetNodeId_.has_value()) {
      editSelectedClipHandler_(selectedClipTargetNodeId_.value(), intent());
    } else if (editSelectedTextClipHandler_ && selectedTextClipTargetNodeId_.has_value()) {
      editSelectedTextClipHandler_(selectedTextClipTargetNodeId_.value(), intent());
    } else if (editSelectedNoteHandler_ && selectedNoteTargetNodeId_.has_value()) {
      editSelectedNoteHandler_(selectedNoteTargetNodeId_.value(), intent());
    }
  });

  recentEdits_ = new QListWidget;
  recentEdits_->setObjectName("stewardRecentEdits");
  recentEdits_->setSelectionMode(QAbstractItemView::SingleSelection);
  recentEdits_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  recentEdits_->setTextElideMode(Qt::ElideRight);
  recentEdits_->setMinimumHeight(52);
  recentEdits_->setMaximumHeight(72);
  layout->addWidget(recentEdits_);
  connect(recentEdits_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
    if (item == nullptr || !selectEditTargetHandler_) {
      return;
    }
    selectEditTargetHandler_(foundation::NodeId{item->data(Qt::UserRole).toString().toStdString()});
  });
  connect(recentEdits_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
    if (item == nullptr || !selectEditTargetHandler_) {
      return;
    }
    selectEditTargetHandler_(foundation::NodeId{item->data(Qt::UserRole).toString().toStdString()});
  });

  text_ = new QTextEdit;
  text_->setObjectName("stewardText");
  text_->setReadOnly(true);
  text_->setMinimumHeight(120);
  text_->setMaximumHeight(180);
  text_->setLineWrapMode(QTextEdit::WidgetWidth);
  layout->addWidget(text_);
}

void StewardPanel::setImportMediaHandler(ImportMediaHandler handler) {
  importMediaHandler_ = std::move(handler);
}

void StewardPanel::setAddCameraHandler(AddCameraHandler handler) {
  addCameraHandler_ = std::move(handler);
}

void StewardPanel::setAddSelectedMediaHandler(AddSelectedMediaHandler handler) {
  addSelectedMediaHandler_ = std::move(handler);
}

void StewardPanel::setShowCameraControlsHandler(ShowCameraControlsHandler handler) {
  showCameraControlsHandler_ = std::move(handler);
}

void StewardPanel::setCreateCameraEffectHandler(CreateCameraEffectHandler handler) {
  createCameraEffectHandler_ = std::move(handler);
}

void StewardPanel::setAdjustCameraControlsHandler(AdjustCameraControlsHandler handler) {
  adjustCameraControlsHandler_ = std::move(handler);
}

void StewardPanel::setEditSelectedClipHandler(EditSelectedClipHandler handler) {
  editSelectedClipHandler_ = std::move(handler);
}

void StewardPanel::setTryEditSelectedClipHandler(TryEditSelectedClipHandler handler) {
  tryEditSelectedClipHandler_ = std::move(handler);
}

void StewardPanel::setEditSelectedTextClipHandler(EditSelectedTextClipHandler handler) {
  editSelectedTextClipHandler_ = std::move(handler);
}

void StewardPanel::setTryEditSelectedTextClipHandler(TryEditSelectedTextClipHandler handler) {
  tryEditSelectedTextClipHandler_ = std::move(handler);
}

void StewardPanel::setTryCreateTextClipHandler(TryCreateTextClipHandler handler) {
  tryCreateTextClipHandler_ = std::move(handler);
}

void StewardPanel::setEditSelectedNoteHandler(EditSelectedNoteHandler handler) {
  editSelectedNoteHandler_ = std::move(handler);
}

void StewardPanel::setTryEditSelectedNoteHandler(TryEditSelectedNoteHandler handler) {
  tryEditSelectedNoteHandler_ = std::move(handler);
}

void StewardPanel::setTryCreateNoteHandler(TryCreateNoteHandler handler) {
  tryCreateNoteHandler_ = std::move(handler);
}

void StewardPanel::setSelectEditTargetHandler(SelectEditTargetHandler handler) {
  selectEditTargetHandler_ = std::move(handler);
}

void StewardPanel::setViewModel(
  const app::AppViewModel& viewModel,
  const agent::AgentConversationState& conversationState,
  const std::optional<foundation::NodeId>& selectedNodeId,
  const std::optional<foundation::AssetId>& selectedAssetId
) {
  const std::optional<foundation::NodeId> cameraTargetId = app::stewardCameraTargetId(viewModel, selectedNodeId);
  selectedClipTargetNodeId_ = selectedVisualClipNodeId(viewModel, selectedNodeId);
  selectedTextClipTargetNodeId_ = selectedTextClipNodeId(viewModel, selectedNodeId);
  selectedNoteTargetNodeId_ = selectedNoteNodeId(viewModel, selectedNodeId);
  selectedTargetActionButton_->setVisible(
    selectedClipTargetNodeId_.has_value() ||
    selectedTextClipTargetNodeId_.has_value() ||
    selectedNoteTargetNodeId_.has_value()
  );
  primaryTargetCameraNodeId_ = cameraTargetId;
  if (!cameraTargetId.has_value()) {
    primaryTargetCameraNodeId_ = std::nullopt;
    if (viewModel.timeline.clips.empty()) {
      if (selectedAssetId.has_value()) {
        primaryAction_ = PrimaryAction::AddSelectedMedia;
        primaryActionButton_->setText("Add Selected Media To Timeline");
      } else if (viewModel.assets.count == 0) {
        primaryAction_ = PrimaryAction::ImportMedia;
        primaryActionButton_->setText("Import Media");
      } else {
        primaryAction_ = PrimaryAction::Disabled;
        primaryActionButton_->setText("Select Media To Add");
      }
    } else if (viewModel.timeline.cameras.empty()) {
      primaryAction_ = PrimaryAction::AddCamera;
      primaryActionButton_->setText("Add Camera");
    } else {
      primaryAction_ = PrimaryAction::Disabled;
      primaryActionButton_->setText("Select Camera");
    }
  } else if (app::cameraHasTransformEffect(viewModel, cameraTargetId.value())) {
    if (selectedNodeId.has_value() && selectedNodeId.value() == cameraTargetId.value()) {
      primaryAction_ = PrimaryAction::AdjustCameraControls;
      primaryActionButton_->setText("Apply Request To Camera Controls");
    } else {
      primaryAction_ = PrimaryAction::ShowCameraControls;
      primaryActionButton_->setText("Show Editable Controls");
    }
  } else {
    primaryAction_ = PrimaryAction::CreateCameraEffect;
    primaryActionButton_->setText("Create Editable Camera Controls");
  }

  QString nextStep;
  const bool selectedTargetActionAvailable =
    selectedClipTargetNodeId_.has_value() ||
    selectedTextClipTargetNodeId_.has_value() ||
    selectedNoteTargetNodeId_.has_value();
  const QString targetChoiceStep = selectedTextClipTargetNodeId_.has_value()
    ? "Next: type a camera request, or use the text action to edit the selected text clip."
    : selectedNoteTargetNodeId_.has_value()
      ? "Next: type a camera request, or use the note action to edit the selected note."
      : "Next: type a camera request, or mention clip/video to edit the selected clip.";
  switch (primaryAction_) {
    case PrimaryAction::ImportMedia:
      nextStep = "Next: import media to start the timeline.";
      break;
    case PrimaryAction::AddSelectedMedia:
      nextStep = "Next: add the selected media to the timeline.";
      break;
    case PrimaryAction::AddCamera:
      nextStep = "Next: add a camera for editable framing.";
      break;
    case PrimaryAction::ShowCameraControls:
      nextStep = selectedTargetActionAvailable
        ? targetChoiceStep
        : "Next: show the camera effect controls.";
      break;
    case PrimaryAction::CreateCameraEffect:
      nextStep = selectedTargetActionAvailable
        ? targetChoiceStep
        : "Next: type the camera edit request, then create editable camera controls.";
      break;
    case PrimaryAction::AdjustCameraControls:
      nextStep = selectedTargetActionAvailable
        ? targetChoiceStep
        : "Next: type the camera edit request, then apply it to the exposed controls.";
      break;
    case PrimaryAction::Disabled:
      nextStep = "Next: select the project item needed for the edit.";
      break;
  }

  QStringList lines{
    "Steward",
    QString{"Project: %1 assets | %2 clips | %3 cameras | %4 editable effects"}
      .arg(viewModel.assets.count)
      .arg(viewModel.timeline.clips.size())
      .arg(viewModel.timeline.cameras.size())
      .arg(viewModel.timeline.effectCount),
    nextStep
  };
  if (const app::AppStewardEditRow* latestEdit = latestTargetedEdit(viewModel)) {
    lines << QString{"Latest result: %1 (%2)"}
      .arg(stewardEditResultLabel(*latestEdit))
      .arg(qString(latestEdit->revision.value()));
    if (!latestEdit->controlSummary.empty()) {
      lines << QString{"Controls changed: %1"}.arg(stewardEditControlSummary(*latestEdit));
    }
    lines << QString{"Latest request: %1"}.arg(stewardEditRequest(*latestEdit));
  }
  if (selectedAssetId.has_value() && primaryAction_ == PrimaryAction::AddSelectedMedia) {
    lines << QString{"Selected asset: %1"}.arg(assetName(viewModel, selectedAssetId.value()));
  }
  if (cameraTargetId.has_value()) {
    lines << QString{"Camera target: %1"}.arg(cameraName(viewModel, cameraTargetId.value()));
  }
  if (selectedClipTargetNodeId_.has_value()) {
    lines << QString{"Clip target: %1"}.arg(clipName(viewModel, selectedClipTargetNodeId_.value()));
    lines << "Clip route: mention clip/video, or use the clip action, to update clip parameters.";
  }
  if (selectedTextClipTargetNodeId_.has_value()) {
    lines << QString{"Text target: %1"}.arg(textClipName(viewModel, selectedTextClipTargetNodeId_.value()));
    lines << "Text route: use the text action to update text clip parameters.";
  }
  if (selectedNoteTargetNodeId_.has_value()) {
    lines << QString{"Note target: %1"}.arg(noteName(viewModel, selectedNoteTargetNodeId_.value()));
    lines << "Note route: use the note action to update the note title or body.";
  }

  recentEdits_->blockSignals(true);
  recentEdits_->clear();
  int selectedEditRow = -1;
  for (auto edit = viewModel.steward.edits.rbegin(); edit != viewModel.steward.edits.rend(); ++edit) {
    if (!edit->targetNodeId.has_value()) {
      continue;
    }
    auto* item = new QListWidgetItem{
      QString{"%1 %2%3%4"}
        .arg(qString(edit->revision.value()))
        .arg(stewardEditRequest(*edit))
        .arg(stewardEditResult(*edit))
        .arg(edit->controlSummary.empty() ? QString{} : QString{" [%1]"}.arg(stewardEditControlSummary(*edit)))
    };
    item->setData(Qt::UserRole, qString(edit->targetNodeId->value()));
    item->setToolTip(item->text());
    recentEdits_->addItem(item);
    if (selectedEditRow == -1 && selectedNodeId.has_value() && selectedNodeId.value() == edit->targetNodeId.value()) {
      selectedEditRow = recentEdits_->count() - 1;
    }
  }
  recentEdits_->setCurrentRow(selectedEditRow);
  recentEdits_->setVisible(recentEdits_->count() > 0);
  recentEdits_->blockSignals(false);

  if (recentEdits_->count() == 0) {
    lines << "Applied edits: none";
  } else {
    lines << "Applied edits: select one to inspect its target.";
  }

  if (conversationState.runs.empty()) {
    lines << "Recent runs: none";
  } else {
    lines << "Recent runs:";
    for (auto run = conversationState.runs.rbegin(); run != conversationState.runs.rend(); ++run) {
      lines << QString{"- %1 [%2]"}
        .arg(qString(run->title))
        .arg(runStatusText(run->status));
      if (!run->summary.empty()) {
        lines << QString{"  %1"}.arg(qString(run->summary));
      }
      for (const agent::AgentConversationMessage& message : run->messages) {
        lines << QString{"  %1: %2"}
          .arg(qString(message.role))
          .arg(qString(message.content));
      }
      for (const agent::AgentConversationToolCall& toolCall : run->toolCalls) {
        QString toolLine = QString{"  %1 -> %2"}
          .arg(qString(toolCall.toolDisplayName))
          .arg(toolStatusText(toolCall.status));
        if (toolCall.observedRevision.has_value()) {
          toolLine += QString{" at %1"}.arg(qString(toolCall.observedRevision->value()));
        }
        lines << toolLine;
      }
      for (const agent::AgentConversationDiagnostic& diagnostic : run->diagnostics) {
        lines << QString{"  diagnostic [%1] %2: %3"}
          .arg(diagnosticSeverityText(diagnostic.severity))
          .arg(qString(diagnostic.code))
          .arg(qString(diagnostic.message));
      }
    }
  }

  if (!conversationState.diagnostics.empty()) {
    lines << "";
    lines << "Conversation projection diagnostics";
    for (const agent::AgentConversationProjectionDiagnostic& diagnostic : conversationState.diagnostics) {
      lines << QString{"- %1: %2"}
        .arg(qString(diagnostic.code))
        .arg(qString(diagnostic.message));
    }
  }

  updateActionLabels();
  updateIntentPlaceholder();
  text_->setPlainText(lines.join('\n'));
  updateActionButtons();
}

void StewardPanel::setIntent(std::string intent) {
  intent_->setPlainText(qString(intent));
  intent_->moveCursor(QTextCursor::Start);
  updateActionButtons();
}

void StewardPanel::triggerPrimaryAction() {
  primaryActionButton_->click();
}

void StewardPanel::triggerSelectedTargetAction() {
  selectedTargetActionButton_->click();
}

std::string StewardPanel::contents() const {
  return text_->toPlainText().toStdString();
}

std::string StewardPanel::intent() const {
  return intent_->toPlainText().toStdString();
}

std::string StewardPanel::intentPlaceholder() const {
  return intent_->placeholderText().toStdString();
}

std::string StewardPanel::primaryActionText() const {
  return primaryActionButton_->text().toStdString();
}

bool StewardPanel::primaryActionEnabled() const {
  return primaryActionButton_->isEnabled();
}

std::string StewardPanel::selectedTargetActionText() const {
  return selectedTargetActionButton_->text().toStdString();
}

bool StewardPanel::selectedTargetActionEnabled() const {
  return selectedTargetActionButton_->isVisible() && selectedTargetActionButton_->isEnabled();
}

void StewardPanel::triggerRecentEdit(int row) {
  if (row < 0 || row >= recentEdits_->count()) {
    return;
  }
  recentEdits_->setCurrentRow(row);
  auto* item = recentEdits_->item(row);
  if (item != nullptr && selectEditTargetHandler_) {
    selectEditTargetHandler_(foundation::NodeId{item->data(Qt::UserRole).toString().toStdString()});
  }
}

int StewardPanel::recentEditCount() const {
  return recentEdits_->count();
}

int StewardPanel::currentRecentEditRow() const {
  return recentEdits_->currentRow();
}

std::string StewardPanel::recentEditText(int row) const {
  if (row < 0 || row >= recentEdits_->count()) {
    return {};
  }
  const QListWidgetItem* item = recentEdits_->item(row);
  return item == nullptr ? std::string{} : item->text().toStdString();
}

void StewardPanel::updateActionButtons() {
  primaryActionButton_->setEnabled(primaryActionCanRun());
  selectedTargetActionButton_->setEnabled(selectedTargetActionCanRun());
}

void StewardPanel::updateActionLabels() {
  const bool hasIntent = intentHasText();
  switch (primaryAction_) {
    case PrimaryAction::CreateCameraEffect:
      primaryActionButton_->setText(hasIntent ? "Create Editable Camera Controls" : "Type Request To Create Camera Controls");
      break;
    case PrimaryAction::AdjustCameraControls:
      primaryActionButton_->setText(hasIntent ? "Apply Request To Camera Controls" : "Type Request To Apply Camera Controls");
      break;
    default:
      break;
  }

  selectedTargetActionButton_->setText(
    selectedTextClipTargetNodeId_.has_value()
      ? (hasIntent ? "Apply Request To Text" : "Type Request To Edit Text")
      : selectedNoteTargetNodeId_.has_value()
        ? (hasIntent ? "Apply Request To Note" : "Type Request To Edit Note")
        : (hasIntent ? "Apply Request To Clip" : "Type Request To Edit Clip")
  );
}

void StewardPanel::updateIntentPlaceholder() {
  const bool selectedTargetActionAvailable = selectedClipTargetNodeId_.has_value();
  switch (primaryAction_) {
    case PrimaryAction::ImportMedia:
      intent_->setPlaceholderText("Try: \"add note \\\"Camera rationale\\\" saying Keep zoom editable\", or import media to start the timeline.");
      return;
    case PrimaryAction::AddSelectedMedia:
      intent_->setPlaceholderText("Add selected media to the timeline. Then try: \"center the subject\" or \"add title \\\"Opening\\\"\".");
      return;
    case PrimaryAction::AddCamera:
      intent_->setPlaceholderText("Try: \"add title \\\"Opening\\\"\", or add a camera for editable framing.");
      return;
    case PrimaryAction::ShowCameraControls:
      intent_->setPlaceholderText(
        selectedTargetActionAvailable
          ? "Try: \"add title \\\"Opening\\\"\", \"zoom in a little\", or use the clip action for \"rotate clip slightly left\"."
          : "Try: \"add title \\\"Opening\\\"\", or show camera controls for \"zoom in a little\"."
      );
      return;
    case PrimaryAction::CreateCameraEffect:
      intent_->setPlaceholderText(
        selectedTargetActionAvailable
          ? "Try: \"add title \\\"Opening\\\"\", \"center the subject\", or use the clip action for \"speed up clip\"."
          : "Try: \"add title \\\"Opening\\\"\", \"center the subject\", or \"slowly pan right\"."
      );
      return;
    case PrimaryAction::AdjustCameraControls:
      intent_->setPlaceholderText(
        selectedTargetActionAvailable
          ? "Try: \"add title \\\"Opening\\\"\", \"move far right\", \"zoom in a little\", or use the clip action for \"move clip later\"."
          : "Try: \"add title \\\"Opening\\\"\", \"move far right\", \"zoom in a little\", \"reset camera\", or \"slowly pan left\"."
      );
      return;
    case PrimaryAction::Disabled:
      intent_->setPlaceholderText("Try: \"add note \\\"Camera rationale\\\" saying Keep zoom editable\", or select media, a clip, or a camera.");
      return;
  }
}

bool StewardPanel::tryEditSelectedClipFromPrimaryAction() {
  if (!selectedClipTargetNodeId_.has_value() ||
      !intentHasText() ||
      !tryEditSelectedClipHandler_) {
    return false;
  }

  return tryEditSelectedClipHandler_(selectedClipTargetNodeId_.value(), intent());
}

bool StewardPanel::tryEditSelectedTextClipFromPrimaryAction() {
  if (!selectedTextClipTargetNodeId_.has_value() ||
      !intentHasText() ||
      !tryEditSelectedTextClipHandler_) {
    return false;
  }

  return tryEditSelectedTextClipHandler_(selectedTextClipTargetNodeId_.value(), intent());
}

bool StewardPanel::tryEditSelectedNoteFromPrimaryAction() {
  if (!selectedNoteTargetNodeId_.has_value() ||
      !intentHasText() ||
      !tryEditSelectedNoteHandler_) {
    return false;
  }

  return tryEditSelectedNoteHandler_(selectedNoteTargetNodeId_.value(), intent());
}

bool StewardPanel::tryCreateTextClipFromPrimaryAction() {
  if (!intentHasText() || !tryCreateTextClipHandler_) {
    return false;
  }
  if (primaryAction_ == PrimaryAction::ImportMedia ||
      primaryAction_ == PrimaryAction::AddSelectedMedia ||
      primaryAction_ == PrimaryAction::Disabled) {
    return false;
  }

  return tryCreateTextClipHandler_(intent());
}

bool StewardPanel::tryCreateNoteFromPrimaryAction() {
  if (!intentHasText() || !tryCreateNoteHandler_) {
    return false;
  }

  return tryCreateNoteHandler_(intent());
}

bool StewardPanel::intentHasText() const {
  return containsNonWhitespace(intent());
}

bool StewardPanel::primaryActionCanRun() const {
  switch (primaryAction_) {
    case PrimaryAction::ImportMedia:
      return static_cast<bool>(importMediaHandler_);
    case PrimaryAction::AddSelectedMedia:
      return static_cast<bool>(addSelectedMediaHandler_);
    case PrimaryAction::AddCamera:
      return static_cast<bool>(addCameraHandler_);
    case PrimaryAction::ShowCameraControls:
      return static_cast<bool>(showCameraControlsHandler_) && primaryTargetCameraNodeId_.has_value();
    case PrimaryAction::CreateCameraEffect:
      return static_cast<bool>(createCameraEffectHandler_) && intentHasText();
    case PrimaryAction::AdjustCameraControls:
      return static_cast<bool>(adjustCameraControlsHandler_) && primaryTargetCameraNodeId_.has_value() && intentHasText();
    case PrimaryAction::Disabled:
      return false;
  }

  return false;
}

bool StewardPanel::selectedTargetActionCanRun() const {
  if (!intentHasText()) {
    return false;
  }
  if (selectedClipTargetNodeId_.has_value()) {
    return static_cast<bool>(editSelectedClipHandler_);
  }
  if (selectedTextClipTargetNodeId_.has_value()) {
    return static_cast<bool>(editSelectedTextClipHandler_);
  }
  return selectedNoteTargetNodeId_.has_value() &&
         static_cast<bool>(editSelectedNoteHandler_);
}

} // namespace grapple::ui
