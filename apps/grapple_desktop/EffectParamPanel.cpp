#include "EffectParamPanel.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace grapple::desktop {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

} // namespace

EffectParamPanel::EffectParamPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("effectParams");
  layout_ = new QVBoxLayout{this};
  layout_->setContentsMargins(12, 12, 12, 12);
  layout_->setSpacing(10);
}

void EffectParamPanel::setApplyHandler(ApplyHandler handler) {
  applyHandler_ = std::move(handler);
}

void EffectParamPanel::setSelection(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  clearControls();
  if (!selectedNodeId.has_value()) {
    addMessage("Select a timeline item to edit its agent-authored controls.");
    return;
  }

  bool hasAttachedEffect = false;
  for (const app::AppEffectGraphRow& graph : viewModel.timeline.effectGraphs) {
    if (graph.targetNodeId != selectedNodeId.value()) {
      continue;
    }

    for (const app::AppEffectRow& effect : graph.effects) {
      hasAttachedEffect = true;
      auto* effectTitle = new QLabel{QString{"%1 Parameters"}.arg(qString(effect.displayName))};
      effectTitle->setObjectName("effectParamTitle");
      layout_->addWidget(effectTitle);

      if (effect.params.empty()) {
        auto* empty = new QLabel{"This effect has no exposed parameters."};
        empty->setObjectName("effectParamHelp");
        layout_->addWidget(empty);
        continue;
      }

      for (const app::AppEffectParamRow& param : effect.params) {
        const QString displayName = param.label.empty()
          ? qString(param.name)
          : qString(param.label);

        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout{row};
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        auto* label = new QLabel{displayName};
        label->setMinimumWidth(92);
        label->setToolTip(qString(param.name));

        auto* editor = new QLineEdit{qString(param.value)};
        editor->setObjectName("effectParamEditor");
        if (param.numericMin.has_value() && param.numericMax.has_value()) {
          editor->setToolTip(QString{"%1..%2"}.arg(*param.numericMin).arg(*param.numericMax));
        }

        auto* apply = new QPushButton{"Apply"};
        apply->setObjectName("effectParamApply");
        const foundation::NodeId effectNodeId = effect.sourceNodeId;
        const std::string paramName = param.name;
        connect(apply, &QPushButton::clicked, this, [this, effectNodeId, paramName, editor] {
          if (applyHandler_) {
            applyHandler_(effectNodeId, paramName, editor->text().toStdString());
          }
        });

        rowLayout->addWidget(label);
        rowLayout->addWidget(editor, 1);
        rowLayout->addWidget(apply);
        layout_->addWidget(row);
      }
    }
  }

  if (!hasAttachedEffect) {
    addMessage("No effects are attached to the selected timeline item.");
  }
  layout_->addStretch(1);
}

void EffectParamPanel::clearControls() {
  while (QLayoutItem* item = layout_->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      delete widget;
    }
    delete item;
  }
}

void EffectParamPanel::addMessage(const QString& message) {
  auto* title = new QLabel{"Effect Parameters"};
  title->setObjectName("effectParamTitle");
  auto* help = new QLabel{message};
  help->setObjectName("effectParamHelp");
  help->setWordWrap(true);
  layout_->addWidget(title);
  layout_->addWidget(help);
}

} // namespace grapple::desktop
