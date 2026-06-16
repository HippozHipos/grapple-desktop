#pragma once

#include <grapple/effects/OutputNames.hpp>

namespace grapple::effects::builtin_effect {

inline constexpr char CameraTransformDisplayName[] = "Camera Transform";
inline constexpr char CameraTransformEntrypoint[] = "camera_transform";
inline constexpr char CameraTransformSource[] = "builtin:camera_transform";
inline constexpr char PositionXParam[] = "position_x";
inline constexpr char PositionYParam[] = "position_y";
inline constexpr char ZoomParam[] = "zoom";
inline constexpr char PositionXLabel[] = "Position X";
inline constexpr char PositionYLabel[] = "Position Y";
inline constexpr char ZoomLabel[] = "Zoom";

inline constexpr char ClipTintDisplayName[] = "Clip Tint";
inline constexpr char ClipTintEntrypoint[] = "clip_tint";
inline constexpr char ClipTintSource[] = "builtin:clip_tint";
inline constexpr char ClipTintColorParam[] = "color";
inline constexpr char ClipTintAmountParam[] = "amount";
inline constexpr char ClipTintColorLabel[] = "Tint Color";
inline constexpr char ClipTintAmountLabel[] = "Tint Amount";

inline constexpr char ClipExposureDisplayName[] = "Clip Exposure";
inline constexpr char ClipExposureEntrypoint[] = "clip_exposure";
inline constexpr char ClipExposureSource[] = "builtin:clip_exposure";
inline constexpr char ClipExposureParam[] = "exposure";
inline constexpr char ClipExposureLabel[] = "Exposure";

} // namespace grapple::effects::builtin_effect
