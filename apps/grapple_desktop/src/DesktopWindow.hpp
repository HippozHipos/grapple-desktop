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
  void clickFirstTimelineTrack();
  void clickFirstTimelineAudioTrack();
  void clickFirstTimelineClip();
  void clickFirstTimelineAudioClip();
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
  void updateSelectedCameraName(std::string name);
  void updateSelectedCameraFocalLength(double focalLength);
  void addNote();
  void updateSelectedNote(std::string title, std::string markdown);
  void importMediaFile(const foundation::FilePath& path);
  void addSelectedMediaToTimeline();
  void deleteSelectedClip();
  void deleteSelectedTrack();
  void moveSelectedClip(foundation::TimeSeconds delta);
  void trimSelectedClipEnd(foundation::TimeSeconds delta);
  void nudgeSelectedClipX(double delta);
  void nudgeSelectedClipY(double delta);
  void setSelectedClipUniformScale(double scale);
  void setSelectedClipOpacity(double opacity);
  void setSelectedClipTransformControlValue(std::string controlName, double value);
  void undoLastEdit();
  void redoLastEdit();
  void exportVideoFile(const foundation::FilePath& path);
  void setEffectParamControlValue(const std::string& paramName, double value);
  void setEffectParamKeyframeAtPlayhead(const std::string& paramName);
  void deleteEffectParamKeyframeControl(const std::string& paramName, int keyframeIndex);
  void setSelectedTargetNumericEffectParam(const std::string& paramName, double value);
  void deleteSelectedTargetEffect();
  void openPackageRoot(const foundation::FilePath& rootPath);

private:
  std::unique_ptr<DesktopWindowImpl> impl_;
};

} // namespace grapple::desktop
