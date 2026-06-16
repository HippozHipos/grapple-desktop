#include <grapple/ui_qt/EffectParamPanel.hpp>

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
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

bool sameNumericValue(double left, double right) {
  return std::abs(left - right) < 0.000001;
}

constexpr int ParamSliderTicks = 1000;
constexpr const char DisplayedValueProperty[] = "effectParamDisplayedValue";
constexpr const char CurrentKeyframeIdProperty[] = "effectParamCurrentKeyframeId";
constexpr const char EffectNodeIdProperty[] = "effectNodeId";
constexpr const char EffectParamNameProperty[] = "effectParamName";
constexpr const char EffectParamKeyframeIndexProperty[] = "effectParamKeyframeIndex";

int sliderIndexForParamValue(double value, double min, double max) {
  if (max <= min) {
    return 0;
  }
  const double normalized = std::clamp((value - min) / (max - min), 0.0, 1.0);
  return static_cast<int>(std::lround(normalized * ParamSliderTicks));
}

double paramValueForSliderIndex(int index, double min, double max) {
  if (max <= min) {
    return min;
  }
  const double normalized = static_cast<double>(std::clamp(index, 0, ParamSliderTicks)) /
                            static_cast<double>(ParamSliderTicks);
  return min + ((max - min) * normalized);
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

QString keyframeActionText(bool hasCurrentKeyframe) {
  return hasCurrentKeyframe ? "Update Key" : "Add Key";
}

QString keyframeActionTooltip(bool hasCurrentKeyframe) {
  return hasCurrentKeyframe ? "Update keyframe at playhead" : "Set keyframe at playhead";
}

void setCurrentKeyframeId(
  QPushButton* button,
  std::optional<foundation::KeyframeId> keyframeId
) {
  button->setText(keyframeActionText(keyframeId.has_value()));
  button->setToolTip(keyframeActionTooltip(keyframeId.has_value()));
  button->setProperty(
    CurrentKeyframeIdProperty,
    keyframeId.has_value() ? qString(keyframeId->value()) : QString{}
  );
}

std::optional<foundation::KeyframeId> currentKeyframeIdFromButton(const QPushButton* button) {
  const QString value = button->property(CurrentKeyframeIdProperty).toString();
  if (value.isEmpty()) {
    return std::nullopt;
  }
  return foundation::KeyframeId{value.toStdString()};
}

QString effectParamObjectName(
  const char* prefix,
  const foundation::NodeId& effectNodeId,
  const std::string& paramName
) {
  return QString{"%1_%2_%3"}
    .arg(QString::fromUtf8(prefix))
    .arg(qString(effectNodeId.value()))
    .arg(qString(paramName));
}

void tagEffectParamWidget(
  QWidget* widget,
  const foundation::NodeId& effectNodeId,
  const std::string& paramName
) {
  widget->setProperty(EffectNodeIdProperty, qString(effectNodeId.value()));
  widget->setProperty(EffectParamNameProperty, qString(paramName));
}

template <typename Widget>
Widget* findEffectParamWidget(
  QWidget* root,
  const char* prefix,
  const foundation::NodeId& effectNodeId,
  const std::string& paramName
) {
  return root->findChild<Widget*>(effectParamObjectName(prefix, effectNodeId, paramName));
}

bool displayedValueMatches(const QWidget* editor, const timeline::ParamValue& value) {
  const QVariant displayedValue = editor->property(DisplayedValueProperty);
  return std::visit(
    [&](const auto& typedValue) -> bool {
      using Value = std::decay_t<decltype(typedValue)>;
      if constexpr (std::is_same_v<Value, double>) {
        return sameNumericValue(typedValue, displayedValue.toDouble());
      } else if constexpr (std::is_same_v<Value, bool>) {
        return typedValue == displayedValue.toBool();
      } else if constexpr (std::is_same_v<Value, std::string>) {
        return qString(typedValue) == displayedValue.toString();
      } else if constexpr (std::is_same_v<Value, foundation::Vec2> ||
                           std::is_same_v<Value, foundation::Vec3> ||
                           std::is_same_v<Value, foundation::Rect>) {
        return qString(app::paramValueDisplayText(value)) == displayedValue.toString();
      }
      return false;
    },
    value
  );
}

void appendLastEditedRow(QVBoxLayout* layout, const app::AppEffectParamRow& param) {
  if (!param.lastEditedRevision.has_value()) {
    return;
  }

  auto* lastEdited = new QLabel{
    QString{"Last changed by %1 at %2"}
      .arg(qString(param.lastEditedActorName.empty() ? param.lastEditedSourceKind : param.lastEditedActorName))
      .arg(qString(param.lastEditedRevision->value()))
  };
  lastEdited->setObjectName("effectParamHelp");
  layout->addWidget(lastEdited);
}

QDoubleSpinBox* makeVectorComponentEditor(
  const foundation::NodeId& effectNodeId,
  const std::string& paramName,
  const char* objectNamePrefix,
  double value
) {
  auto* editor = new QDoubleSpinBox;
  editor->setObjectName(effectParamObjectName(objectNamePrefix, effectNodeId, paramName));
  tagEffectParamWidget(editor, effectNodeId, paramName);
  editor->setButtonSymbols(QAbstractSpinBox::NoButtons);
  editor->setRange(-10000.0, 10000.0);
  editor->setDecimals(3);
  editor->setSingleStep(0.01);
  editor->setKeyboardTracking(false);
  editor->setFixedWidth(74);
  editor->setValue(value);
  return editor;
}

} // namespace

EffectParamPanel::EffectParamPanel(QWidget* parent)
  : QWidget{parent} {
  setObjectName("effectParams");
  layout_ = new QVBoxLayout{this};
  layout_->setContentsMargins(12, 12, 12, 12);
  layout_->setSpacing(10);
  layout_->setSizeConstraint(QLayout::SetMinimumSize);
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

void EffectParamPanel::commitEditedValue(
  const QWidget* editor,
  const QPushButton* keyframeButton,
  foundation::NodeId effectNodeId,
  std::string paramName,
  bool animatedParam,
  timeline::ParamValue value
) {
  if (displayedValueMatches(editor, value)) {
    return;
  }
  if (animatedParam) {
    if (setKeyframeHandler_) {
      setKeyframeHandler_(std::move(effectNodeId), std::move(paramName), std::move(value), currentKeyframeIdFromButton(keyframeButton));
    }
    return;
  }

  if (applyHandler_) {
    applyHandler_(std::move(effectNodeId), std::move(paramName), std::move(value));
  }
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
      effectTitle->setWordWrap(true);
      auto* deleteEffect = new QPushButton{"Delete"};
      deleteEffect->setToolTip("Delete effect");
      deleteEffect->setObjectName("effectParamDelete");
      deleteEffect->setFixedWidth(72);
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
        const bool animatedParam = !param.keyframes.empty();
        const timeline::ParamValue displayedValue = app::sampledEffectParamValue(param, playhead);
        const QString displayName = param.label.empty()
          ? qString(param.name)
          : qString(param.label);

        auto appendKeyframeRows = [&] {
          for (std::size_t keyframeIndex = 0; keyframeIndex < param.keyframes.size(); ++keyframeIndex) {
            const app::AppEffectParamRow::Keyframe& keyframe = param.keyframes[keyframeIndex];
            auto* keyframeRow = new QWidget;
            auto* keyframeLayout = new QHBoxLayout{keyframeRow};
            keyframeLayout->setContentsMargins(12, 0, 0, 0);
            keyframeLayout->setSpacing(8);

            QString keyframeText = QString{"%1 = %2"}
              .arg(timeText(keyframe.time))
              .arg(qString(app::paramValueDisplayText(keyframe.value)));
            if (keyframe.lastEditedRevision.has_value()) {
              keyframeText += QString{" last changed by %1 at %2"}
                .arg(qString(keyframe.lastEditedActorName.empty() ? keyframe.lastEditedSourceKind : keyframe.lastEditedActorName))
                .arg(qString(keyframe.lastEditedRevision->value()));
            }
            auto* keyframeLabel = new QLabel{keyframeText};
            keyframeLabel->setObjectName("effectParamHelp");
            auto* deleteKeyframe = new QPushButton{"Delete"};
            deleteKeyframe->setToolTip("Delete keyframe");
            deleteKeyframe->setObjectName(QString{"effectParamDeleteKeyframe_%1_%2_%3"}
              .arg(qString(parameterEffectNodeId.value()))
              .arg(qString(param.name))
              .arg(static_cast<int>(keyframeIndex)));
            tagEffectParamWidget(deleteKeyframe, parameterEffectNodeId, param.name);
            deleteKeyframe->setProperty(EffectParamKeyframeIndexProperty, static_cast<int>(keyframeIndex));
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
        auto* rowLayout = new QVBoxLayout{row};
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* label = new QLabel{displayName};
        label->setToolTip(qString(param.name));
        rowLayout->addWidget(label);

        const auto* numericValue = std::get_if<double>(&displayedValue);
        if (numericValue != nullptr && param.numericMin.has_value() && param.numericMax.has_value()) {
          auto* controlRow = new QWidget;
          auto* controlLayout = new QHBoxLayout{controlRow};
          controlLayout->setContentsMargins(0, 0, 0, 0);
          controlLayout->setSpacing(8);
          auto* editor = new QDoubleSpinBox;
          editor->setObjectName(effectParamObjectName("effectParamEditor", parameterEffectNodeId, param.name));
          tagEffectParamWidget(editor, parameterEffectNodeId, param.name);
          editor->setButtonSymbols(QAbstractSpinBox::NoButtons);
          editor->setRange(*param.numericMin, *param.numericMax);
          editor->setDecimals(3);
          editor->setKeyboardTracking(false);
          editor->setFixedWidth(74);
          if (param.numericStep.has_value()) {
            editor->setSingleStep(*param.numericStep);
          }
          editor->setValue(*numericValue);
          editor->setProperty(DisplayedValueProperty, *numericValue);
          editor->setToolTip(QString{"%1..%2"}.arg(*param.numericMin).arg(*param.numericMax));
          auto* slider = new QSlider{Qt::Horizontal};
          slider->setObjectName(effectParamObjectName("effectParamSlider", parameterEffectNodeId, param.name));
          tagEffectParamWidget(slider, parameterEffectNodeId, param.name);
          slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
          slider->setRange(0, ParamSliderTicks);
          slider->setValue(sliderIndexForParamValue(*numericValue, *param.numericMin, *param.numericMax));
          slider->setEnabled(*param.numericMax > *param.numericMin);
          auto* setKeyframe = new QPushButton;
          setKeyframe->setFixedWidth(82);
          setKeyframe->setObjectName(effectParamObjectName("effectParamKeyframe", parameterEffectNodeId, param.name));
          tagEffectParamWidget(setKeyframe, parameterEffectNodeId, param.name);
          setCurrentKeyframeId(setKeyframe, currentKeyframeId);

          auto commitEditedValue = [this, editor, setKeyframe, parameterEffectNodeId, paramName, animatedParam](timeline::ParamValue value) {
            this->commitEditedValue(editor, setKeyframe, parameterEffectNodeId, paramName, animatedParam, std::move(value));
          };

          connect(editor, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [slider, min = *param.numericMin, max = *param.numericMax](double value) {
            const QSignalBlocker blockSlider{slider};
            slider->setValue(sliderIndexForParamValue(value, min, max));
          });
          connect(editor, &QDoubleSpinBox::editingFinished, this, [editor, commitEditedValue] {
            const double value = editor->value();
            commitEditedValue(timeline::ParamValue{value});
          });
          connect(slider, &QSlider::valueChanged, this, [editor, min = *param.numericMin, max = *param.numericMax](int index) {
            const QSignalBlocker blockEditor{editor};
            editor->setValue(paramValueForSliderIndex(index, min, max));
          });
          connect(slider, &QSlider::sliderReleased, this, [this, editor, commitEditedValue] {
            const double value = editor->value();
            commitEditedValue(timeline::ParamValue{value});
          });
          connect(setKeyframe, &QPushButton::clicked, this, [this, editor, setKeyframe, parameterEffectNodeId, paramName] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{editor->value()}, currentKeyframeIdFromButton(setKeyframe));
            }
          });

          controlLayout->addWidget(editor, 0);
          controlLayout->addWidget(slider, 1);
          controlLayout->addWidget(setKeyframe);
          rowLayout->addWidget(controlRow);
          layout_->addWidget(row);
          appendLastEditedRow(layout_, param);
          appendKeyframeRows();
          continue;
        }

        if (const auto* boolValue = std::get_if<bool>(&displayedValue)) {
          auto* controlRow = new QWidget;
          auto* controlLayout = new QHBoxLayout{controlRow};
          controlLayout->setContentsMargins(0, 0, 0, 0);
          controlLayout->setSpacing(8);
          auto* editor = new QCheckBox;
          editor->setObjectName(effectParamObjectName("effectParamEditor", parameterEffectNodeId, param.name));
          tagEffectParamWidget(editor, parameterEffectNodeId, param.name);
          editor->setChecked(*boolValue);
          editor->setProperty(DisplayedValueProperty, *boolValue);
          auto* setKeyframe = new QPushButton;
          setKeyframe->setFixedWidth(82);
          setKeyframe->setObjectName(effectParamObjectName("effectParamKeyframe", parameterEffectNodeId, param.name));
          tagEffectParamWidget(setKeyframe, parameterEffectNodeId, param.name);
          setCurrentKeyframeId(setKeyframe, currentKeyframeId);

          auto commitEditedValue = [this, editor, setKeyframe, parameterEffectNodeId, paramName, animatedParam](timeline::ParamValue value) {
            this->commitEditedValue(editor, setKeyframe, parameterEffectNodeId, paramName, animatedParam, std::move(value));
          };

          connect(editor, &QCheckBox::toggled, this, [commitEditedValue](bool value) {
            commitEditedValue(timeline::ParamValue{value});
          }, Qt::QueuedConnection);
          connect(setKeyframe, &QPushButton::clicked, this, [this, editor, setKeyframe, parameterEffectNodeId, paramName] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{editor->isChecked()}, currentKeyframeIdFromButton(setKeyframe));
            }
          });

          controlLayout->addWidget(editor, 1);
          controlLayout->addWidget(setKeyframe);
          rowLayout->addWidget(controlRow);
          layout_->addWidget(row);
          appendLastEditedRow(layout_, param);
          appendKeyframeRows();
          continue;
        }

        if (const auto* stringValue = std::get_if<std::string>(&displayedValue)) {
          auto* controlRow = new QWidget;
          auto* controlLayout = new QHBoxLayout{controlRow};
          controlLayout->setContentsMargins(0, 0, 0, 0);
          controlLayout->setSpacing(8);
          auto* editor = new QLineEdit{qString(*stringValue)};
          editor->setObjectName(effectParamObjectName("effectParamEditor", parameterEffectNodeId, param.name));
          tagEffectParamWidget(editor, parameterEffectNodeId, param.name);
          editor->setProperty(DisplayedValueProperty, qString(*stringValue));
          auto* setKeyframe = new QPushButton;
          setKeyframe->setFixedWidth(82);
          setKeyframe->setObjectName(effectParamObjectName("effectParamKeyframe", parameterEffectNodeId, param.name));
          tagEffectParamWidget(setKeyframe, parameterEffectNodeId, param.name);
          setCurrentKeyframeId(setKeyframe, currentKeyframeId);

          auto commitEditedValue = [this, editor, setKeyframe, parameterEffectNodeId, paramName, animatedParam](timeline::ParamValue value) {
            this->commitEditedValue(editor, setKeyframe, parameterEffectNodeId, paramName, animatedParam, std::move(value));
          };

          connect(editor, &QLineEdit::editingFinished, this, [editor, commitEditedValue] {
            commitEditedValue(timeline::ParamValue{editor->text().toStdString()});
          });
          connect(setKeyframe, &QPushButton::clicked, this, [this, editor, setKeyframe, parameterEffectNodeId, paramName] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(parameterEffectNodeId, paramName, timeline::ParamValue{editor->text().toStdString()}, currentKeyframeIdFromButton(setKeyframe));
            }
          });

          controlLayout->addWidget(editor, 1);
          controlLayout->addWidget(setKeyframe);
          rowLayout->addWidget(controlRow);
          layout_->addWidget(row);
          appendLastEditedRow(layout_, param);
          appendKeyframeRows();
          continue;
        }

        if (const auto* vec2Value = std::get_if<foundation::Vec2>(&displayedValue)) {
          auto* controlRow = new QWidget;
          auto* controlLayout = new QHBoxLayout{controlRow};
          controlLayout->setContentsMargins(0, 0, 0, 0);
          controlLayout->setSpacing(8);
          controlRow->setObjectName(effectParamObjectName("effectParamEditor", parameterEffectNodeId, param.name));
          tagEffectParamWidget(controlRow, parameterEffectNodeId, param.name);
          controlRow->setProperty(DisplayedValueProperty, qString(app::paramValueDisplayText(displayedValue)));
          auto* xEditor = makeVectorComponentEditor(parameterEffectNodeId, param.name, "effectParamVecX", vec2Value->x);
          auto* yEditor = makeVectorComponentEditor(parameterEffectNodeId, param.name, "effectParamVecY", vec2Value->y);
          auto* setKeyframe = new QPushButton;
          setKeyframe->setFixedWidth(82);
          setKeyframe->setObjectName(effectParamObjectName("effectParamKeyframe", parameterEffectNodeId, param.name));
          tagEffectParamWidget(setKeyframe, parameterEffectNodeId, param.name);
          setCurrentKeyframeId(setKeyframe, currentKeyframeId);

          auto commitVector = [this, controlRow, setKeyframe, xEditor, yEditor, parameterEffectNodeId, paramName, animatedParam] {
            this->commitEditedValue(
              controlRow,
              setKeyframe,
              parameterEffectNodeId,
              paramName,
              animatedParam,
              timeline::ParamValue{foundation::Vec2{xEditor->value(), yEditor->value()}}
            );
          };
          connect(xEditor, &QDoubleSpinBox::editingFinished, this, commitVector);
          connect(yEditor, &QDoubleSpinBox::editingFinished, this, commitVector);
          connect(setKeyframe, &QPushButton::clicked, this, [this, setKeyframe, xEditor, yEditor, parameterEffectNodeId, paramName] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(
                parameterEffectNodeId,
                paramName,
                timeline::ParamValue{foundation::Vec2{xEditor->value(), yEditor->value()}},
                currentKeyframeIdFromButton(setKeyframe)
              );
            }
          });

          controlLayout->addWidget(new QLabel{"X"});
          controlLayout->addWidget(xEditor);
          controlLayout->addWidget(new QLabel{"Y"});
          controlLayout->addWidget(yEditor);
          controlLayout->addWidget(setKeyframe);
          rowLayout->addWidget(controlRow);
          layout_->addWidget(row);
          appendLastEditedRow(layout_, param);
          appendKeyframeRows();
          continue;
        }

        if (const auto* vec3Value = std::get_if<foundation::Vec3>(&displayedValue)) {
          auto* controlRow = new QWidget;
          auto* controlLayout = new QHBoxLayout{controlRow};
          controlLayout->setContentsMargins(0, 0, 0, 0);
          controlLayout->setSpacing(8);
          controlRow->setObjectName(effectParamObjectName("effectParamEditor", parameterEffectNodeId, param.name));
          tagEffectParamWidget(controlRow, parameterEffectNodeId, param.name);
          controlRow->setProperty(DisplayedValueProperty, qString(app::paramValueDisplayText(displayedValue)));
          auto* xEditor = makeVectorComponentEditor(parameterEffectNodeId, param.name, "effectParamVecX", vec3Value->x);
          auto* yEditor = makeVectorComponentEditor(parameterEffectNodeId, param.name, "effectParamVecY", vec3Value->y);
          auto* zEditor = makeVectorComponentEditor(parameterEffectNodeId, param.name, "effectParamVecZ", vec3Value->z);
          auto* setKeyframe = new QPushButton;
          setKeyframe->setFixedWidth(82);
          setKeyframe->setObjectName(effectParamObjectName("effectParamKeyframe", parameterEffectNodeId, param.name));
          tagEffectParamWidget(setKeyframe, parameterEffectNodeId, param.name);
          setCurrentKeyframeId(setKeyframe, currentKeyframeId);

          auto commitVector = [this, controlRow, setKeyframe, xEditor, yEditor, zEditor, parameterEffectNodeId, paramName, animatedParam] {
            this->commitEditedValue(
              controlRow,
              setKeyframe,
              parameterEffectNodeId,
              paramName,
              animatedParam,
              timeline::ParamValue{foundation::Vec3{xEditor->value(), yEditor->value(), zEditor->value()}}
            );
          };
          connect(xEditor, &QDoubleSpinBox::editingFinished, this, commitVector);
          connect(yEditor, &QDoubleSpinBox::editingFinished, this, commitVector);
          connect(zEditor, &QDoubleSpinBox::editingFinished, this, commitVector);
          connect(setKeyframe, &QPushButton::clicked, this, [this, setKeyframe, xEditor, yEditor, zEditor, parameterEffectNodeId, paramName] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(
                parameterEffectNodeId,
                paramName,
                timeline::ParamValue{foundation::Vec3{xEditor->value(), yEditor->value(), zEditor->value()}},
                currentKeyframeIdFromButton(setKeyframe)
              );
            }
          });

          controlLayout->addWidget(new QLabel{"X"});
          controlLayout->addWidget(xEditor);
          controlLayout->addWidget(new QLabel{"Y"});
          controlLayout->addWidget(yEditor);
          controlLayout->addWidget(new QLabel{"Z"});
          controlLayout->addWidget(zEditor);
          controlLayout->addWidget(setKeyframe);
          rowLayout->addWidget(controlRow);
          layout_->addWidget(row);
          appendLastEditedRow(layout_, param);
          appendKeyframeRows();
          continue;
        }

        {
          auto* controlRow = new QWidget;
          auto* controlLayout = new QHBoxLayout{controlRow};
          controlLayout->setContentsMargins(0, 0, 0, 0);
          controlLayout->setSpacing(8);
          auto* value = new QLabel{qString(app::paramValueDisplayText(displayedValue))};
          value->setObjectName("effectParamHelp");
          auto* setKeyframe = new QPushButton;
          setKeyframe->setFixedWidth(82);
          setKeyframe->setObjectName(effectParamObjectName("effectParamKeyframe", parameterEffectNodeId, param.name));
          tagEffectParamWidget(setKeyframe, parameterEffectNodeId, param.name);
          setCurrentKeyframeId(setKeyframe, currentKeyframeId);
          const timeline::ParamValue paramValue = displayedValue;
          connect(setKeyframe, &QPushButton::clicked, this, [this, setKeyframe, parameterEffectNodeId, paramName, paramValue] {
            if (setKeyframeHandler_) {
              setKeyframeHandler_(parameterEffectNodeId, paramName, paramValue, currentKeyframeIdFromButton(setKeyframe));
            }
          });
          controlLayout->addWidget(value, 1);
          controlLayout->addWidget(setKeyframe);
          rowLayout->addWidget(controlRow);
          layout_->addWidget(row);
          appendLastEditedRow(layout_, param);
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

void EffectParamPanel::refreshPlayheadValues(
  const app::AppViewModel& viewModel,
  const std::optional<foundation::NodeId>& selectedNodeId,
  foundation::TimeSeconds playhead
) {
  if (!selectedNodeId.has_value()) {
    return;
  }

  for (const app::AppEffectGraphRow& graph : viewModel.timeline.effectGraphs) {
    if (graph.targetNodeId != selectedNodeId.value()) {
      continue;
    }

    for (const app::AppEffectRow& effect : graph.effects) {
      for (const app::AppEffectParamRow& param : effect.params) {
        const timeline::ParamValue displayedValue = app::sampledEffectParamValue(param, playhead);
        const std::optional<foundation::KeyframeId> currentKeyframeId = keyframeIdAtPlayhead(param, playhead);
        auto* keyframeButton = findEffectParamWidget<QPushButton>(
          this,
          "effectParamKeyframe",
          effect.sourceNodeId,
          param.name
        );
        if (keyframeButton != nullptr) {
          setCurrentKeyframeId(keyframeButton, currentKeyframeId);
        }

        if (const auto* numericValue = std::get_if<double>(&displayedValue)) {
          auto* editor = findEffectParamWidget<QDoubleSpinBox>(
            this,
            "effectParamEditor",
            effect.sourceNodeId,
            param.name
          );
          if (editor != nullptr) {
            const QSignalBlocker blockEditor{editor};
            editor->setValue(*numericValue);
            editor->setProperty(DisplayedValueProperty, *numericValue);
          }
          auto* slider = findEffectParamWidget<QSlider>(
            this,
            "effectParamSlider",
            effect.sourceNodeId,
            param.name
          );
          if (slider != nullptr && param.numericMin.has_value() && param.numericMax.has_value()) {
            const QSignalBlocker blockSlider{slider};
            slider->setValue(sliderIndexForParamValue(*numericValue, *param.numericMin, *param.numericMax));
          }
          continue;
        }

        if (const auto* boolValue = std::get_if<bool>(&displayedValue)) {
          auto* editor = findEffectParamWidget<QCheckBox>(
            this,
            "effectParamEditor",
            effect.sourceNodeId,
            param.name
          );
          if (editor != nullptr) {
            const QSignalBlocker blockEditor{editor};
            editor->setChecked(*boolValue);
            editor->setProperty(DisplayedValueProperty, *boolValue);
          }
          continue;
        }

        if (const auto* stringValue = std::get_if<std::string>(&displayedValue)) {
          auto* editor = findEffectParamWidget<QLineEdit>(
            this,
            "effectParamEditor",
            effect.sourceNodeId,
            param.name
          );
          if (editor != nullptr) {
            const QSignalBlocker blockEditor{editor};
            editor->setText(qString(*stringValue));
            editor->setProperty(DisplayedValueProperty, qString(*stringValue));
          }
          continue;
        }

        if (const auto* vec2Value = std::get_if<foundation::Vec2>(&displayedValue)) {
          auto* xEditor = findEffectParamWidget<QDoubleSpinBox>(
            this,
            "effectParamVecX",
            effect.sourceNodeId,
            param.name
          );
          auto* yEditor = findEffectParamWidget<QDoubleSpinBox>(
            this,
            "effectParamVecY",
            effect.sourceNodeId,
            param.name
          );
          auto* editor = findEffectParamWidget<QWidget>(
            this,
            "effectParamEditor",
            effect.sourceNodeId,
            param.name
          );
          if (xEditor != nullptr && yEditor != nullptr && editor != nullptr) {
            const QSignalBlocker blockX{xEditor};
            const QSignalBlocker blockY{yEditor};
            xEditor->setValue(vec2Value->x);
            yEditor->setValue(vec2Value->y);
            editor->setProperty(DisplayedValueProperty, qString(app::paramValueDisplayText(displayedValue)));
          }
          continue;
        }

        if (const auto* vec3Value = std::get_if<foundation::Vec3>(&displayedValue)) {
          auto* xEditor = findEffectParamWidget<QDoubleSpinBox>(
            this,
            "effectParamVecX",
            effect.sourceNodeId,
            param.name
          );
          auto* yEditor = findEffectParamWidget<QDoubleSpinBox>(
            this,
            "effectParamVecY",
            effect.sourceNodeId,
            param.name
          );
          auto* zEditor = findEffectParamWidget<QDoubleSpinBox>(
            this,
            "effectParamVecZ",
            effect.sourceNodeId,
            param.name
          );
          auto* editor = findEffectParamWidget<QWidget>(
            this,
            "effectParamEditor",
            effect.sourceNodeId,
            param.name
          );
          if (xEditor != nullptr && yEditor != nullptr && zEditor != nullptr && editor != nullptr) {
            const QSignalBlocker blockX{xEditor};
            const QSignalBlocker blockY{yEditor};
            const QSignalBlocker blockZ{zEditor};
            xEditor->setValue(vec3Value->x);
            yEditor->setValue(vec3Value->y);
            zEditor->setValue(vec3Value->z);
            editor->setProperty(DisplayedValueProperty, qString(app::paramValueDisplayText(displayedValue)));
          }
        }
      }
    }
  }
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
