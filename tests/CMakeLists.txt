add_subdirectory(unit/foundation)
add_subdirectory(unit/graph)
add_subdirectory(unit/project)
add_subdirectory(unit/projection)

add_test(
  NAME architecture_guards
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_architecture.sh
)

add_test(NAME grapple_cli_smoke COMMAND grapple_cli)
