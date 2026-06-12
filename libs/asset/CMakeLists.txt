add_library(grapple_asset
  src/AssetCatalog.cpp
  src/AssetSerializer.cpp
)

add_library(Grapple::Asset ALIAS grapple_asset)

target_include_directories(grapple_asset
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_asset
  PUBLIC
    Grapple::Foundation
)
