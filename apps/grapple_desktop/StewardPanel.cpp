#include "StewardPanel.hpp"

#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include <utility>

namespace grapple::desktop {

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
      return qString(clip.assetId.value());
    }
  }
  for (const app::AppLayerRow& layer : viewModel.timeline.layers) {
    if (layer.sourceNodeId == nodeId) {
      return qString(layer.name);
    }
  }
  return qString(nodeId.value());
}

QStringList exposedControlsFor(const app::AppEffectRow& effect) {
  QStringList controls;
  for (const app::AppEffectParamRow& param : effect.params) {
    controls << (param.label.empty() ? qString(param.name) : qString(param.label));
  }
  return controls;
}

} // namespace

StewardPanel::StewardPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("stewardPanel");

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);

  intent_ = new QLineEdit{"Center the subject with an editable camera transform."};
  intent_->setObjectName("stewardIntent");
  intent_->setCursorPosition(0);
  layout->addWidget(intent_);

  createCameraEffectButton_ = new QPushButton{"Steward: Create Camera Transform"};
  createCameraEffectButton_->setObjectName("stewardCreateCameraEffect");
  layout->addWidget(createCameraEffectButton_);
  connect(createCameraEffectButton_, &QPushButton::clicked, this, [this] {
    if (createCameraEffectHandler_) {
      createCameraEffectHandler_(intent());
    }
  });

  text_ = new QTextEdit;
  text_->setObjectName("stewardText");
  text_->setReadOnly(true);
  text_->setMinimumHeight(160);
  text_->setLineWrapMode(QTextEdit::WidgetWidth);
  layout->addWidget(text_);
}

void StewardPanel::setCreateCameraEffectHandler(CreateCameraEffectHandler handler) {
  createCameraEffectHandler_ = std::move(handler);
}

void StewardPanel::setViewModel(const app::AppViewModel& viewModel) {
  QStringList lines{
    "Steward",
    "Bet: prompt -> editable graph",
    QString{"Intent: %1"}.arg(qString(intent())),
    "Action: selected camera -> editable Camera Transform",
    QString{"%1 | %2 clips | %3 cameras | %4 effects"}
      .arg(qString(viewModel.project.revision.value()))
      .arg(viewModel.timeline.clips.size())
      .arg(viewModel.timeline.cameras.size())
      .arg(viewModel.timeline.effectGraphs.size()),
    "",
    "Editable agent outputs"
  };

  bool hasEditableEffect = false;
  for (const app::AppEffectGraphRow& graph : viewModel.timeline.effectGraphs) {
    for (const app::AppEffectRow& effect : graph.effects) {
      const QStringList controls = exposedControlsFor(effect);
      if (controls.empty()) {
        continue;
      }

      hasEditableEffect = true;
      lines << QString{"- %1 / %2"}
        .arg(qString(effect.displayName))
        .arg(targetNameFor(viewModel, graph.targetNodeId));
      lines << QString{"  %1"}.arg(controls.join(", "));
    }
  }

  if (!hasEditableEffect) {
    lines << "- none yet";
  }

  text_->setPlainText(lines.join('\n'));
}

void StewardPanel::setIntent(std::string intent) {
  intent_->setText(qString(intent));
  intent_->setCursorPosition(0);
}

void StewardPanel::triggerCreateCameraEffect() {
  createCameraEffectButton_->click();
}

std::string StewardPanel::contents() const {
  return text_->toPlainText().toStdString();
}

std::string StewardPanel::intent() const {
  return intent_->text().toStdString();
}

} // namespace grapple::desktop
