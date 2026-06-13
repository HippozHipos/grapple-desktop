#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/StrongId.hpp>
#include <grapple/foundation/Time.hpp>

#include <QPixmap>

#include <memory>
#include <optional>
#include <string>

namespace grapple::app {
class NativeWorkspaceSession;
}

namespace grapple::desktop {

class DesktopWindowImpl;

class DesktopWindow final {
public:
  explicit DesktopWindow(grapple::app::NativeWorkspaceSession& workspace);
  ~DesktopWindow();

  DesktopWindow(const DesktopWindow&) = delete;
  DesktopWindow& operator=(const DesktopWindow&) = delete;
  DesktopWindow(DesktopWindow&&) = delete;
  DesktopWindow& operator=(DesktopWindow&&) = delete;

  void show();
  [[nodiscard]] QPixmap grab() const;

  void seekTo(foundation::TimeSeconds time);
  void clickTimelineAtRatio(double ratio);
  void clickFirstTimelineClip();
  void clickFirstTimelineCamera();
  void clickSecondTimelineCamera();
  [[nodiscard]] std::optional<foundation::NodeId> selectedNodeId() const;
  [[nodiscard]] std::optional<foundation::AssetId> selectedAssetId() const;
  [[nodiscard]] std::string inspectorContents() const;
  [[nodiscard]] std::string logContents() const;
  [[nodiscard]] std::string stewardContents() const;
  void setStewardIntent(std::string intent);
  void clickStewardCreateCameraEffect();
  void startPlayback();
  void pausePlayback();
  void advancePlaybackFrame();
  void addTrack();
  void addCamera();
  void importMediaFile(const foundation::FilePath& path);
  void addSelectedMediaToTimeline();
  void deleteSelectedClip();
  void moveSelectedClip(foundation::TimeSeconds delta);
  void undoLastEdit();
  void redoLastEdit();
  void exportVideoFile(const foundation::FilePath& path);
  void setEffectParamControlValue(const std::string& paramName, double value);
  void setSelectedTargetNumericEffectParam(const std::string& paramName, double value);
  void deleteSelectedTargetEffect();
  void openPackageRoot(const foundation::FilePath& rootPath);

private:
  std::unique_ptr<DesktopWindowImpl> impl_;
};

} // namespace grapple::desktop
