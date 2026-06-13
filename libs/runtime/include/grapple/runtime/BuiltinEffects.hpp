#pragma once

#include <grapple/runtime/RuntimeOutputNames.hpp>

namespace grapple::runtime::builtin_effect {

inline constexpr char CameraTransformDisplayName[] = "Camera Transform";
inline constexpr char CameraTransformEntrypoint[] = "camera_transform";
inline constexpr char CameraTransformSource[] = "builtin:camera_transform";
inline constexpr char PositionXParam[] = "position_x";
inline constexpr char PositionYParam[] = "position_y";
inline constexpr char ZoomParam[] = "zoom";
inline constexpr char PositionXLabel[] = "Position X";
inline constexpr char PositionYLabel[] = "Position Y";
inline constexpr char ZoomLabel[] = "Zoom";

} // namespace grapple::runtime::builtin_effect
