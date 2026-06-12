#include <grapple/asset/AssetSerializer.hpp>

#include <grapple/foundation/Json.hpp>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace grapple::asset {

namespace {

void writeNumber(std::ostringstream& stream, double value) {
  stream << std::setprecision(17) << value;
}

const char* mediaTypeName(AssetMediaType mediaType) {
  switch (mediaType) {
    case AssetMediaType::Video:
      return "video";
    case AssetMediaType::Audio:
      return "audio";
    case AssetMediaType::Image:
      return "image";
  }

  std::abort();
}

void writeOptionalFilePath(std::ostringstream& stream, const std::optional<foundation::FilePath>& filePath) {
  if (filePath.has_value()) {
    stream << foundation::jsonQuoted(filePath->value);
  } else {
    stream << "null";
  }
}

void writeOptionalDuration(std::ostringstream& stream, const std::optional<foundation::TimeSeconds>& duration) {
  if (duration.has_value()) {
    writeNumber(stream, duration->value);
  } else {
    stream << "null";
  }
}

void writeOptionalResolution(std::ostringstream& stream, const std::optional<foundation::Resolution>& resolution) {
  if (resolution.has_value()) {
    stream << "{\"width\":" << resolution->width << ",\"height\":" << resolution->height << '}';
  } else {
    stream << "null";
  }
}

void writeOptionalFrameRate(std::ostringstream& stream, const std::optional<foundation::FrameRate>& frameRate) {
  if (frameRate.has_value()) {
    stream << "{\"numerator\":" << frameRate->numerator << ",\"denominator\":" << frameRate->denominator << '}';
  } else {
    stream << "null";
  }
}

} // namespace

std::string serializeCanonicalAsset(const Asset& asset) {
  std::ostringstream stream;
  stream << '{';
  foundation::writeJsonStringProperty(stream, "id", asset.id.value());
  stream << ',';
  foundation::writeJsonStringProperty(stream, "name", asset.name);
  stream << ",\"metadata\":{";
  foundation::writeJsonStringProperty(stream, "mediaType", mediaTypeName(asset.metadata.mediaType));
  stream << ',';
  foundation::writeJsonStringProperty(stream, "sourcePath", asset.metadata.sourcePath.value);
  stream << ",\"thumbnailPath\":";
  writeOptionalFilePath(stream, asset.metadata.thumbnailPath);
  stream << ",\"duration\":";
  writeOptionalDuration(stream, asset.metadata.duration);
  stream << ",\"dimensions\":";
  writeOptionalResolution(stream, asset.metadata.dimensions);
  stream << ",\"frameRate\":";
  writeOptionalFrameRate(stream, asset.metadata.frameRate);
  stream << "}}";
  return stream.str();
}

std::string serializeCanonicalAssetCatalog(const AssetCatalog& catalog) {
  std::vector<Asset> assets = catalog.assets();
  std::sort(assets.begin(), assets.end(), [](const Asset& left, const Asset& right) {
    return left.id < right.id;
  });

  std::ostringstream stream;
  stream << '[';
  for (std::size_t index = 0; index < assets.size(); ++index) {
    if (index != 0) {
      stream << ',';
    }
    stream << serializeCanonicalAsset(assets[index]);
  }
  stream << ']';
  return stream.str();
}

} // namespace grapple::asset
