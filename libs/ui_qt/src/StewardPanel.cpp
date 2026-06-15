#include <grapple/ui_qt/StewardPanel.hpp>

#include <QListWidget>
#include <QPushButton>
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

bool containsNonWhitespace(const std::string& value) {
  return std::any_of(value.begin(), value.end(), [](unsigned char character) {
    return std::isspace(character) == 0;
  });
}

} // namespace

StewardPanel::StewardPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("stewardPanel");

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);

  intent_ = new QTextEdit;
  intent_->setObjectName("stewardIntent");
  intent_->setPlaceholderText("Describe the edit you want Steward to apply.");
  intent_->setAcceptRichText(false);
  intent_->setLineWrapMode(QTextEdit::WidgetWidth);
  intent_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  intent_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  intent_->setMinimumHeight(48);
  intent_->setMaximumHeight(64);
  layout->addWidget(intent_);
  connect(intent_, &QTextEdit::textChanged, this, [this] {
    updateActionButtons();
  });

  primaryActionButton_ = new QPushButton{"Create Editable Camera Controls"};
  primaryActionButton_->setObjectName("stewardPrimaryAction");
  layout->addWidget(primaryActionButton_);
  connect(primaryActionButton_, &QPushButton::clicked, this, [this] {
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

  selectedClipActionButton_ = new QPushButton{"Apply Request To Clip Transform"};
  selectedClipActionButton_->setObjectName("stewardSelectedClipAction");
  layout->addWidget(selectedClipActionButton_);
  connect(selectedClipActionButton_, &QPushButton::clicked, this, [this] {
    if (transformSelectedClipHandler_ && selectedClipTargetNodeId_.has_value()) {
      transformSelectedClipHandler_(selectedClipTargetNodeId_.value(), intent());
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
  text_->setMinimumHeight(72);
  text_->setMaximumHeight(96);
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

void StewardPanel::setTransformSelectedClipHandler(TransformSelectedClipHandler handler) {
  transformSelectedClipHandler_ = std::move(handler);
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
  selectedClipActionButton_->setVisible(selectedClipTargetNodeId_.has_value());
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
  const bool selectedClipActionAvailable = selectedClipTargetNodeId_.has_value();
  const QString targetChoiceStep = "Next: choose camera controls or selected clip transform for this request.";
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
      nextStep = selectedClipActionAvailable
        ? targetChoiceStep
        : "Next: show the camera effect controls.";
      break;
    case PrimaryAction::CreateCameraEffect:
      nextStep = selectedClipActionAvailable
        ? targetChoiceStep
        : "Next: create editable camera controls from the request.";
      break;
    case PrimaryAction::AdjustCameraControls:
      nextStep = selectedClipActionAvailable
        ? targetChoiceStep
        : "Next: apply the request to the exposed camera controls.";
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
  if (selectedClipTargetNodeId_.has_value()) {
    lines << "Selected clip action: apply the request to clip transform parameters.";
  }

  recentEdits_->blockSignals(true);
  recentEdits_->clear();
  int selectedEditRow = -1;
  for (auto edit = viewModel.steward.edits.rbegin(); edit != viewModel.steward.edits.rend(); ++edit) {
    if (!edit->targetNodeId.has_value()) {
      continue;
    }
    const QString editText = edit->editName.empty()
      ? QString{"Edit"}
      : qString(edit->editName);
    const QString requestText = edit->intent.empty()
      ? editText
      : qString(edit->intent);
    const QString targetText = edit->targetName.empty()
      ? QString{}
      : QString{" on %1"}.arg(qString(edit->targetName));
    const QString resultText = edit->targetName.empty() && edit->editName.empty()
      ? QString{}
      : QString{" -> %1%2"}.arg(editText).arg(targetText);
    auto* item = new QListWidgetItem{
      QString{"%1 %2%3"}.arg(qString(edit->revision.value())).arg(requestText).arg(resultText)
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

void StewardPanel::triggerSelectedClipAction() {
  selectedClipActionButton_->click();
}

std::string StewardPanel::contents() const {
  return text_->toPlainText().toStdString();
}

std::string StewardPanel::intent() const {
  return intent_->toPlainText().toStdString();
}

std::string StewardPanel::primaryActionText() const {
  return primaryActionButton_->text().toStdString();
}

bool StewardPanel::primaryActionEnabled() const {
  return primaryActionButton_->isEnabled();
}

std::string StewardPanel::selectedClipActionText() const {
  return selectedClipActionButton_->text().toStdString();
}

bool StewardPanel::selectedClipActionEnabled() const {
  return selectedClipActionButton_->isVisible() && selectedClipActionButton_->isEnabled();
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
  selectedClipActionButton_->setEnabled(selectedClipActionCanRun());
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

bool StewardPanel::selectedClipActionCanRun() const {
  return selectedClipTargetNodeId_.has_value() &&
         static_cast<bool>(transformSelectedClipHandler_) &&
         intentHasText();
}

} // namespace grapple::ui
