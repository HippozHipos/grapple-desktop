add_subdirectory(unit/foundation)
add_subdirectory(unit/graph)
add_subdirectory(unit/project)

add_test(
  NAME architecture_guards
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_architecture.sh
)
