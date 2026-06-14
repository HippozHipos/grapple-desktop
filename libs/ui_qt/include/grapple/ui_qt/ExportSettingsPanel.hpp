#pragma once

#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/render/ExportSettings.hpp>

#include <QWidget>

#include <functional>
#include <string>

class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QVBoxLayout;

namespace grapple::ui {

struct ExportSettingsDraft {
  foundation::Resolution resolution{1920, 1080};
  foundation::FrameRate frameRate{30, 1};
  render::Codec codec{"mjpeg"};
};

class ExportSettingsPanel final : public QWidget {
public:
  using ApplyHandler = std::function<void(ExportSettingsDraft)>;

  explicit ExportSettingsPanel(QWidget* parent = nullptr);

  void setApplyHandler(ApplyHandler handler);
  [[nodiscard]] ExportSettingsDraft draft() const;

private:
  QSpinBox* addIntegerEditor(
    const QString& labelText,
    const QString& objectName,
    int value,
    int minimum,
    int maximum
  );
  void emitDraft();

  QVBoxLayout* layout_ = nullptr;
  ApplyHandler applyHandler_;
  QSpinBox* width_ = nullptr;
  QSpinBox* height_ = nullptr;
  QDoubleSpinBox* framesPerSecond_ = nullptr;
  QComboBox* codec_ = nullptr;
};

} // namespace grapple::ui
