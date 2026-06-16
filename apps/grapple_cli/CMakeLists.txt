add_executable(grapple_cli
  main.cpp
)

set_target_properties(grapple_cli PROPERTIES
  OUTPUT_NAME grapple-cli
)

target_compile_definitions(grapple_cli
  PRIVATE
    GRAPPLE_NATIVE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(grapple_cli
  PRIVATE
    Grapple::App
    Grapple::DemoProject
    Grapple::Projection
)

install(TARGETS grapple_cli
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
