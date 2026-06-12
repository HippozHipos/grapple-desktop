#include <grapple/asset/AssetCatalog.hpp>
#include <grapple/asset/AssetSerializer.hpp>

#include <TestAssert.hpp>

int main() {
  using namespace grapple;

  asset::AssetCatalog catalog;

  const auto registered = catalog.registerAsset(asset::Asset{
    foundation::AssetId{"asset_video"},
    "Walking Woman",
    asset::AssetMetadata{
      asset::AssetMediaType::Video,
      foundation::FilePath{"/media/walking-woman.mp4"},
      foundation::FilePath{"/media/walking-woman.jpg"},
      foundation::TimeSeconds{10.0},
      foundation::Resolution{1080, 1920},
      foundation::FrameRate{30, 1}
    }
  });
  GRAPPLE_REQUIRE(registered);
  GRAPPLE_REQUIRE(catalog.assets().size() == 1);

  const asset::Asset* asset = catalog.find(foundation::AssetId{"asset_video"});
  GRAPPLE_REQUIRE(asset != nullptr);
  GRAPPLE_REQUIRE(asset->name == "Walking Woman");
  GRAPPLE_REQUIRE(asset->metadata.mediaType == asset::AssetMediaType::Video);
  GRAPPLE_REQUIRE(asset->metadata.duration.has_value());
  GRAPPLE_REQUIRE(asset->metadata.duration->value == 10.0);
  GRAPPLE_REQUIRE((asset->metadata.dimensions == foundation::Resolution{1080, 1920}));
  GRAPPLE_REQUIRE(asset->metadata.frameRate->framesPerSecond() == 30.0);
  GRAPPLE_REQUIRE(asset::serializeCanonicalAsset(*asset).find("\"sourcePath\":\"/media/walking-woman.mp4\"") != std::string::npos);
  GRAPPLE_REQUIRE(asset::serializeCanonicalAssetCatalog(catalog).find("\"id\":\"asset_video\"") != std::string::npos);

  const auto duplicate = catalog.registerAsset(asset::Asset{
    foundation::AssetId{"asset_video"},
    "Duplicate",
    asset::AssetMetadata{
      asset::AssetMediaType::Video,
      foundation::FilePath{"/media/duplicate.mp4"},
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt
    }
  });
  GRAPPLE_REQUIRE(!duplicate);
  GRAPPLE_REQUIRE(duplicate.error().code == "asset.id_duplicate");

  const auto missingPath = catalog.registerAsset(asset::Asset{
    foundation::AssetId{"asset_missing_path"},
    "Missing Path",
    asset::AssetMetadata{
      asset::AssetMediaType::Image,
      foundation::FilePath{""},
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt
    }
  });
  GRAPPLE_REQUIRE(!missingPath);
  GRAPPLE_REQUIRE(missingPath.error().code == "asset.source_path_empty");

  return 0;
}
