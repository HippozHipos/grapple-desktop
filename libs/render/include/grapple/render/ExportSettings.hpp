#pragma once

#include <grapple/foundation/FilePath.hpp>
#include <grapple/foundation/Geometry.hpp>
#include <grapple/foundation/Time.hpp>

#include <string>

namespace grapple::render {

enum class ExportQuality {
  Draft,
  Final
};

struct Codec {
  std::string name;
};

struct ExportSettings {
  foundation::TimeRange range;
  foundation::FrameRate frameRate;
  foundation::Resolution resolution;
  Codec codec;
  ExportQuality quality = ExportQuality::Final;
  foundation::FilePath outputPath;
};

} // namespace grapple::render

