#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Time.hpp>
#include <grapple/render/RenderQuality.hpp>

#include <string>

namespace grapple::render {

struct Codec {
  std::string name;
};

struct ExportSettings {
  foundation::TimeRange range;
  foundation::FrameRate frameRate;
  foundation::Resolution resolution;
  Codec codec;
  RenderQuality quality = RenderQuality::Final;
  foundation::FilePath outputPath;
};

} // namespace grapple::render
