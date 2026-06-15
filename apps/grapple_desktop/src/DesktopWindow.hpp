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
  [[nodiscard]] std::string projectHeaderText() const;
  [[nodiscard]] std::string inspectorContents() const;
  [[nodiscard]] std::string logContents() const;
  [[nodiscard]] std::string stewardContents() const;
  [[nodiscard]] std::string stewardPrimaryActionText() const;
  [[nodiscard]] bool stewardPrimaryActionEnabled() const;
  [[nodiscard]] std::string stewardSelectedClipActionText() const;
  [[nodiscard]] bool stewardSelectedClipActionEnabled() const;
  [[nodiscard]] int stewardRecentEditCount() const;
  [[nodiscard]] int stewardCurrentRecentEditRow() const;
  [[nodiscard]] std::string stewardRecentEditText(int row) const;
  [[nodiscard]] std::string effectParamTitleText() const;
  [[nodiscard]] std::string effectParamPanelText() const;
  [[nodiscard]] bool effectParamControlVisible(const std::string& paramName) const;
  [[nodiscard]] std::optional<double> effectParamControlValue(const std::string& paramName) const;
  [[nodiscard]] std::string currentDetailTabText() const;
  [[nodiscard]] std::string stewardIntent() const;
  void setStewardIntent(std::string intent);
  void pressStewardSubmitShortcut();
  void clickStewardPrimaryAction();
  void clickStewardSelectedClipAction();
  void clickStewardRecentEdit(int row);
  void startPlayback();
  void pausePlayback();
  void advancePlaybackFrame();
  void addTrack();
  void addCamera();
  void updateSelectedCameraName(std::string name);
  void updateSelectedCameraFocalLength(double focalLength);
  void setSelectedCameraNameControlValue(std::string name);
  void setSelectedCameraFocalLengthControlValue(double focalLength);
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
  void newPackageRoot(const foundation::FilePath& rootPath, std::string projectName);
  void savePackageAs(const foundation::FilePath& rootPath);
  void exportVideoFile(const foundation::FilePath& path);
  void setExportResolutionControlValue(int width, int height);
  void setExportFrameRateControlValue(double framesPerSecond);
  void setExportCodecControlValue(std::string codec);
  void setEffectParamControlDraftValue(const std::string& paramName, double value);
  void setEffectParamControlValue(const std::string& paramName, double value);
  void setEffectParamSliderRatio(const std::string& paramName, double ratio);
  void setEffectParamKeyframeAtPlayhead(const std::string& paramName);
  [[nodiscard]] std::string effectParamKeyframeButtonText(const std::string& paramName) const;
  void deleteEffectParamKeyframeControl(const std::string& paramName, int keyframeIndex);
  void setSelectedTargetNumericEffectParam(const std::string& paramName, double value);
  void deleteSelectedTargetEffect();
  void openPackageRoot(const foundation::FilePath& rootPath);

private:
  std::unique_ptr<DesktopWindowImpl> impl_;
};

} // namespace grapple::desktop
