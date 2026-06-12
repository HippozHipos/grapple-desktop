add_executable(grapple_cli
  main.cpp
)

target_link_libraries(grapple_cli
  PRIVATE
    Grapple::App
    Grapple::Projection
)
