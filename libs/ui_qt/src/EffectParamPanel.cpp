#include <grapple/ui_qt/EffectParamPanel.hpp>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>
#include <sstream>
#include <utility>
#include <variant>

namespace grapple::ui {

namespace {

QString qString(const std::string& value) {
  return QString::fromStdString(value);
}

QString timeText(foundation::TimeSeconds time) {
  std::ostringstream output;
  output << time.value << "s";
  return qString(output.str());
}

bool sameTime(foundation::TimeSeconds left, foundation::TimeSeconds right) {
  return std::abs(left.value - right.value) < 0.000001;
}

std::optional<foundation::KeyframeId> keyframeIdAtPlayhead(
  const app::AppEffectParamRow& param,
  foundation::TimeSeconds playhead
) {
  for (const app::AppEffectParamRow::Keyframe& keyframe : param.keyframes) {
    if (sameTime(keyframe.time, playhead)) {
      return keyframe.keyframeId;
    }
  }
  return std::nullopt;
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

void EffectParamPanel::setKeyframeHandler(SetKeyframeHandler handler) {
  setKeyframeHandler_ = std::move(handler);
}

void EffectParamPanel::setDeleteKeyframeHandler(DeleteKeyframeHandler handler) {
  deleteKeyframeHandler_ = std::move(handler);
}

void EffectParamPanel::setSelection(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId,
  foundation::TimeSeconds playhead
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

      auto* effectTitle = new QLabel{
        QString{"%1 on %2"}.arg(qString(effect.displayName)).arg(qString(graph.targetName))
      };
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

      if (effect.createdRevision.has_value()) {
        QString provenance = QString{"Created by %1 at %2"}
          .arg(qString(effect.createdActorName.empty() ? effect.createdSourceKind : effect.createdActorName))
          .arg(qString(effect.createdRevision->value()));
        if (!effect.createdIntent.empty()) {
          provenance += QString{": %1"}.arg(qString(effect.createdIntent));
        }
        auto* provenanceLabel = new QLabel{provenance};
        provenanceLabel->setObjectName("effectParamHelp");
        provenanceLabel->setWordWrap(true);
        layout_->addWidget(provenanceLabel);
      }

      if (effect.params.empty()) {
        auto* empty = new QLabel{"This effect has no exposed parameters."};
        empty->setObjectName("effectParamHelp");
        layout_->addWidget(empty);
        continue;
      }

      for (const app::AppEffectParamRow& param : effect.params) {
        const foundation::NodeId parameterEffectNodeId = effect.sourceNodeId;
        const std::string paramName = param.name;
        const std::optional<foundation::KeyframeId> currentKeyframeId = keyframeIdAtPlayhead(param, playhead);
        const QString displayName = param.label.empty()
          ? qString(param.name)
          : qString(param.label);

        auto appendKeyframeRows = [&] {
          for (std::size_t keyframeIndex = 0; keyframeIndex < param.keyframes.size(); ++keyframeIndex) {
            const app::AppEffectParamRow::Keyframe& keyframe = param.keyframes[keyframeIndex];
            auto* keyframeRow = new QWidget;
            auto* keyframeLayout = new QHBoxLayout{keyframeRow};
            keyframeLayout->setContentsMargins(92, 0, 0, 0);
            keyframeLayout->setSpacing(8);

            auto* keyframeLabel = new QLabel{
              QString{"%1 = %2"}
                .arg(timeText(keyframe.time))
                .arg(qString(app::paramValueDisplayText(keyframe.value)))
            };
            keyframeLabel->setObjectName("effectParamHelp");
            auto* deleteKeyframe = new QPushButton{"Delete Keyframe"};
            deleteKeyframe->setObjectName(QString{"effectParamDeleteKeyframe_%1_%2"}
              .arg(qString(param.name))
              .arg(static_cast<int>(keyframeIndex)));
            const foundation::KeyframeId keyframeId = keyframe.keyframeId;
            connect(deleteKeyframe, &QPushButton::clicked, this, [this, parameterEffectNodeId, paramName, keyframeId] {
              if (deleteKeyframeHandler_) {
                deleteKeyframeHandler_(parameterEffectNodeId, paramName, keyframeId);
              }
            });

            keyframeLayout->addWidget(keyframeLabel, 1);
            keyframeLayout->addWidget(deleteKeyframe);
            layout_->addWidget(keyframeRow);
          }
        };

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

          connect(editor, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, parameterEffectNodeId, paramName](double value) {
            if (applyHandler_) {
              applyHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{value});
            }
          }, Qt::QueuedConnection);
          auto* setKeyframe = new QPushButton{currentKeyframeId.has_value() ? "Update Keyframe" : "Set Keyframe"};
          setKeyframe->setObjectName(QString{"effectParamKeyframe_%1"}.arg(qString(param.name)));
          connect(setKeyframe, &QPushButton::clicked, this, [this, editor, parameterEffectNodeId, paramName, currentKeyframeId] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{editor->value()}, currentKeyframeId);
            }
          });

          rowLayout->addWidget(label);
          rowLayout->addWidget(editor, 1);
          rowLayout->addWidget(setKeyframe);
          layout_->addWidget(row);
          appendKeyframeRows();
          continue;
        }

        if (const auto* boolValue = std::get_if<bool>(&param.value)) {
          auto* editor = new QCheckBox;
          editor->setObjectName(QString{"effectParamEditor_%1"}.arg(qString(param.name)));
          editor->setChecked(*boolValue);

          connect(editor, &QCheckBox::toggled, this, [this, parameterEffectNodeId, paramName](bool value) {
            if (applyHandler_) {
              applyHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{value});
            }
          }, Qt::QueuedConnection);
          auto* setKeyframe = new QPushButton{currentKeyframeId.has_value() ? "Update Keyframe" : "Set Keyframe"};
          setKeyframe->setObjectName(QString{"effectParamKeyframe_%1"}.arg(qString(param.name)));
          connect(setKeyframe, &QPushButton::clicked, this, [this, editor, parameterEffectNodeId, paramName, currentKeyframeId] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{editor->isChecked()}, currentKeyframeId);
            }
          });

          rowLayout->addWidget(label);
          rowLayout->addWidget(editor, 1);
          rowLayout->addWidget(setKeyframe);
          layout_->addWidget(row);
          appendKeyframeRows();
          continue;
        }

        if (const auto* stringValue = std::get_if<std::string>(&param.value)) {
          auto* editor = new QLineEdit{qString(*stringValue)};
          editor->setObjectName(QString{"effectParamEditor_%1"}.arg(qString(param.name)));

          connect(editor, &QLineEdit::editingFinished, this, [this, editor, parameterEffectNodeId, paramName] {
            if (applyHandler_) {
              applyHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{editor->text().toStdString()});
            }
          });
          auto* setKeyframe = new QPushButton{currentKeyframeId.has_value() ? "Update Keyframe" : "Set Keyframe"};
          setKeyframe->setObjectName(QString{"effectParamKeyframe_%1"}.arg(qString(param.name)));
          connect(setKeyframe, &QPushButton::clicked, this, [this, editor, parameterEffectNodeId, paramName, currentKeyframeId] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{editor->text().toStdString()}, currentKeyframeId);
            }
          });

          rowLayout->addWidget(label);
          rowLayout->addWidget(editor, 1);
          rowLayout->addWidget(setKeyframe);
          layout_->addWidget(row);
          appendKeyframeRows();
          continue;
        }

        {
          auto* value = new QLabel{qString(app::paramValueDisplayText(param.value))};
          value->setObjectName("effectParamHelp");
          auto* setKeyframe = new QPushButton{currentKeyframeId.has_value() ? "Update Keyframe" : "Set Keyframe"};
          setKeyframe->setObjectName(QString{"effectParamKeyframe_%1"}.arg(qString(param.name)));
          const timeline::ParamValue paramValue = param.value;
          connect(setKeyframe, &QPushButton::clicked, this, [this, parameterEffectNodeId, paramName, paramValue, currentKeyframeId] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(parameterEffectNodeId, paramName, paramValue, currentKeyframeId);
            }
          });
          rowLayout->addWidget(label);
          rowLayout->addWidget(value, 1);
          rowLayout->addWidget(setKeyframe);
          layout_->addWidget(row);
          appendKeyframeRows();
          continue;
        }
      }
    }
  }

  if (!hasAttachedEffect) {
    addMessage("No editable controls yet. Use Steward to create editable camera controls for this selection.");
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
