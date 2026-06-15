#include <grapple/ui_qt/StewardPanel.hpp>

#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <utility>

namespace grapple::ui {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

QString targetNameFor(const app::AppViewModel& viewModel, const foundation::NodeId& nodeId) {
  for (const app::AppCameraRow& camera : viewModel.timeline.cameras) {
    if (camera.sourceNodeId == nodeId) {
      return qString(camera.name);
    }
  }
  for (const app::AppClipRow& clip : viewModel.timeline.clips) {
    if (clip.sourceNodeId == nodeId) {
      return qString(clip.assetName);
    }
  }
  for (const app::AppLayerRow& layer : viewModel.timeline.layers) {
    if (layer.sourceNodeId == nodeId) {
      return qString(layer.name);
    }
  }
  return qString(nodeId.value());
}

QString controlTextFor(const app::AppEffectParamRow& param) {
  const QString displayName = param.label.empty() ? qString(param.name) : qString(param.label);
  QString text = QString{"%1=%2"}.arg(displayName).arg(qString(app::paramValueDisplayText(param.value)));
  if (param.numericMin.has_value() && param.numericMax.has_value()) {
    text += QString{" [%1..%2"}.arg(*param.numericMin).arg(*param.numericMax);
    if (param.numericStep.has_value()) {
      text += QString{" step %1"}.arg(*param.numericStep);
    }
    text += "]";
  }
  return text;
}

QStringList exposedControlsFor(const app::AppEffectRow& effect) {
  QStringList controls;
  for (const app::AppEffectParamRow& param : effect.params) {
    controls << controlTextFor(param);
  }
  return controls;
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

} // namespace

StewardPanel::StewardPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("stewardPanel");

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);

  intent_ = new QTextEdit{"Center the subject with an editable camera transform."};
  intent_->setObjectName("stewardIntent");
  intent_->setAcceptRichText(false);
  intent_->setLineWrapMode(QTextEdit::WidgetWidth);
  intent_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  intent_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  intent_->setMinimumHeight(54);
  intent_->setMaximumHeight(72);
  layout->addWidget(intent_);

  createCameraEffectButton_ = new QPushButton{"Create Editable Camera Controls"};
  createCameraEffectButton_->setObjectName("stewardCreateCameraEffect");
  layout->addWidget(createCameraEffectButton_);
  connect(createCameraEffectButton_, &QPushButton::clicked, this, [this] {
    switch (primaryAction_) {
      case PrimaryAction::AddCamera:
        if (addCameraHandler_) {
          addCameraHandler_();
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
      case PrimaryAction::ControlsShown:
      case PrimaryAction::Disabled:
        return;
    }
  });

  text_ = new QTextEdit;
  text_->setObjectName("stewardText");
  text_->setReadOnly(true);
  text_->setMinimumHeight(160);
  text_->setLineWrapMode(QTextEdit::WidgetWidth);
  layout->addWidget(text_);
}

void StewardPanel::setAddCameraHandler(AddCameraHandler handler) {
  addCameraHandler_ = std::move(handler);
}

void StewardPanel::setShowCameraControlsHandler(ShowCameraControlsHandler handler) {
  showCameraControlsHandler_ = std::move(handler);
}

void StewardPanel::setCreateCameraEffectHandler(CreateCameraEffectHandler handler) {
  createCameraEffectHandler_ = std::move(handler);
}

void StewardPanel::setViewModel(
  const app::AppViewModel& viewModel,
  const agent::AgentConversationState& conversationState,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  const std::optional<foundation::NodeId> cameraTargetId = app::stewardCameraTargetId(viewModel, selectedNodeId);
  primaryTargetCameraNodeId_ = cameraTargetId;
  if (!cameraTargetId.has_value()) {
    primaryTargetCameraNodeId_ = std::nullopt;
    if (viewModel.timeline.cameras.empty()) {
      primaryAction_ = PrimaryAction::AddCamera;
      createCameraEffectButton_->setText("Add Camera");
      createCameraEffectButton_->setEnabled(static_cast<bool>(addCameraHandler_));
    } else {
      primaryAction_ = PrimaryAction::Disabled;
      createCameraEffectButton_->setText("Select Camera");
      createCameraEffectButton_->setEnabled(false);
    }
  } else if (app::cameraHasTransformEffect(viewModel, cameraTargetId.value())) {
    if (selectedNodeId.has_value() && selectedNodeId.value() == cameraTargetId.value()) {
      primaryAction_ = PrimaryAction::ControlsShown;
      createCameraEffectButton_->setText("Editable Controls Shown");
      createCameraEffectButton_->setEnabled(false);
    } else {
      primaryAction_ = PrimaryAction::ShowCameraControls;
      createCameraEffectButton_->setText("Show Editable Controls");
      createCameraEffectButton_->setEnabled(static_cast<bool>(showCameraControlsHandler_));
    }
  } else {
    primaryAction_ = PrimaryAction::CreateCameraEffect;
    createCameraEffectButton_->setText("Create Editable Camera Controls");
    createCameraEffectButton_->setEnabled(static_cast<bool>(createCameraEffectHandler_));
  }

  QStringList lines{
    "Steward",
    "Project state",
    QString{"%1 clips | %2 cameras | %3 editable effects"}
      .arg(viewModel.timeline.clips.size())
      .arg(viewModel.timeline.cameras.size())
      .arg(viewModel.timeline.effectCount),
    "",
    "Applied edits"
  };

  if (viewModel.steward.edits.empty()) {
    lines << "- no Steward edits yet";
  } else {
    for (auto edit = viewModel.steward.edits.rbegin(); edit != viewModel.steward.edits.rend(); ++edit) {
      lines << QString{"- %1: %2"}
        .arg(qString(edit->revision.value()))
        .arg(qString(edit->intent));
    }
  }

  lines << "";
  lines << "Editable controls";

  bool hasEditableEffect = false;
  for (const app::AppEffectGraphRow& graph : viewModel.timeline.effectGraphs) {
    for (const app::AppEffectRow& effect : graph.effects) {
      const QStringList controls = exposedControlsFor(effect);
      if (controls.empty()) {
        continue;
      }

      hasEditableEffect = true;
      lines << QString{"- %1 on %2"}
        .arg(qString(effect.displayName))
        .arg(targetNameFor(viewModel, graph.targetNodeId));
      for (const QString& control : controls) {
        lines << QString{"  - %1"}.arg(control);
      }
    }
  }

  if (!hasEditableEffect) {
    lines << "- no editable controls yet";
  }

  lines << "";
  lines << "Loop";
  lines << "1. Create an editable result";
  lines << "2. Preview the evaluated result";
  lines << "3. Tune exposed parameters without rerunning Steward";
  lines << "";
  lines << "Recent Steward runs";
  if (conversationState.runs.empty()) {
    lines << "- no runs yet";
  } else {
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
        lines << QString{"  project edit -> %1"}.arg(toolStatusText(toolCall.status));
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
}

void StewardPanel::setIntent(std::string intent) {
  intent_->setPlainText(qString(intent));
  intent_->moveCursor(QTextCursor::Start);
}

void StewardPanel::triggerCreateCameraEffect() {
  createCameraEffectButton_->click();
}

std::string StewardPanel::contents() const {
  return text_->toPlainText().toStdString();
}

std::string StewardPanel::intent() const {
  return intent_->toPlainText().toStdString();
}

std::string StewardPanel::primaryActionText() const {
  return createCameraEffectButton_->text().toStdString();
}

bool StewardPanel::primaryActionEnabled() const {
  return createCameraEffectButton_->isEnabled();
}

} // namespace grapple::ui
