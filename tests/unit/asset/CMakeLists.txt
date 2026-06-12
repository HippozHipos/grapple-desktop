add_executable(grapple_asset_tests
  AssetCatalogTest.cpp
)

target_link_libraries(grapple_asset_tests
  PRIVATE
    Grapple::Asset
)

target_include_directories(grapple_asset_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_asset_tests COMMAND grapple_asset_tests)

