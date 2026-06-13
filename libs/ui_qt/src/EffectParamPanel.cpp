#include <grapple/ui_qt/EffectParamPanel.hpp>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>
#include <variant>

namespace grapple::ui {

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

void EffectParamPanel::setDeleteHandler(DeleteHandler handler) {
  deleteHandler_ = std::move(handler);
}

void EffectParamPanel::setSelection(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId
) {
  clearControls();
  if (!selectedNodeId.has_value()) {
    addMessage("Select a camera or clip to tune its editable controls.");
    return;
  }

  bool hasAttachedEffect = false;
  for (const app::AppEffectGraphRow& graph : viewModel.timeline.effectGraphs) {
    if (graph.targetNodeId != selectedNodeId.value()) {
      continue;
    }

    for (const app::AppEffectRow& effect : graph.effects) {
      hasAttachedEffect = true;
      auto* effectHeader = new QWidget;
      auto* effectHeaderLayout = new QHBoxLayout{effectHeader};
      effectHeaderLayout->setContentsMargins(0, 0, 0, 0);
      effectHeaderLayout->setSpacing(8);

      auto* effectTitle = new QLabel{QString{"%1 Parameters"}.arg(qString(effect.displayName))};
      effectTitle->setObjectName("effectParamTitle");
      auto* deleteEffect = new QPushButton{"Delete Effect"};
      deleteEffect->setObjectName("effectParamDelete");
      const foundation::NodeId effectNodeId = effect.sourceNodeId;
      connect(deleteEffect, &QPushButton::clicked, this, [this, effectNodeId] {
        if (deleteHandler_) {
          deleteHandler_(effectNodeId);
        }
      });
      effectHeaderLayout->addWidget(effectTitle, 1);
      effectHeaderLayout->addWidget(deleteEffect);
      layout_->addWidget(effectHeader);

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

        const auto* numericValue = std::get_if<double>(&param.value);
        if (numericValue != nullptr && param.numericMin.has_value() && param.numericMax.has_value()) {
          auto* editor = new QDoubleSpinBox;
          editor->setObjectName(QString{"effectParamEditor_%1"}.arg(qString(param.name)));
          editor->setRange(*param.numericMin, *param.numericMax);
          editor->setDecimals(4);
          editor->setKeyboardTracking(false);
          if (param.numericStep.has_value()) {
            editor->setSingleStep(*param.numericStep);
          }
          editor->setValue(*numericValue);
          editor->setToolTip(QString{"%1..%2"}.arg(*param.numericMin).arg(*param.numericMax));

          const foundation::NodeId parameterEffectNodeId = effect.sourceNodeId;
          const std::string paramName = param.name;
          connect(editor, &QDoubleSpinBox::editingFinished, this, [this, editor, parameterEffectNodeId, paramName] {
            if (applyHandler_) {
              applyHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{editor->value()});
            }
          });

          rowLayout->addWidget(label);
          rowLayout->addWidget(editor, 1);
          layout_->addWidget(row);
          continue;
        }

        if (const auto* boolValue = std::get_if<bool>(&param.value)) {
          auto* editor = new QCheckBox;
          editor->setObjectName(QString{"effectParamEditor_%1"}.arg(qString(param.name)));
          editor->setChecked(*boolValue);

          const foundation::NodeId parameterEffectNodeId = effect.sourceNodeId;
          const std::string paramName = param.name;
          connect(editor, &QCheckBox::toggled, this, [this, parameterEffectNodeId, paramName](bool value) {
            if (applyHandler_) {
              applyHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{value});
            }
          }, Qt::QueuedConnection);

          rowLayout->addWidget(label);
          rowLayout->addWidget(editor, 1);
          layout_->addWidget(row);
          continue;
        }

        if (const auto* stringValue = std::get_if<std::string>(&param.value)) {
          auto* editor = new QLineEdit{qString(*stringValue)};
          editor->setObjectName(QString{"effectParamEditor_%1"}.arg(qString(param.name)));

          const foundation::NodeId parameterEffectNodeId = effect.sourceNodeId;
          const std::string paramName = param.name;
          connect(editor, &QLineEdit::editingFinished, this, [this, editor, parameterEffectNodeId, paramName] {
            if (applyHandler_) {
              applyHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{editor->text().toStdString()});
            }
          });

          rowLayout->addWidget(label);
          rowLayout->addWidget(editor, 1);
          layout_->addWidget(row);
          continue;
        }

        {
          auto* value = new QLabel{qString(app::paramValueDisplayText(param.value))};
          value->setObjectName("effectParamHelp");
          rowLayout->addWidget(label);
          rowLayout->addWidget(value, 1);
          layout_->addWidget(row);
          continue;
        }
      }
    }
  }

  if (!hasAttachedEffect) {
    addMessage("No editable controls yet. Ask Steward to create an edit for this selection.");
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

} // namespace grapple::ui
