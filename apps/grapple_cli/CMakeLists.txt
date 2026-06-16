add_executable(grapple_cli
  main.cpp
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
