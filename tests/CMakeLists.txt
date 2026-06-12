add_subdirectory(unit/foundation)
add_subdirectory(unit/asset)
add_subdirectory(unit/timeline)
add_subdirectory(unit/graph)
add_subdirectory(unit/project)
add_subdirectory(unit/projection)
add_subdirectory(unit/history)
add_subdirectory(unit/storage)
add_subdirectory(unit/jobs)
add_subdirectory(unit/media)
add_subdirectory(unit/runtime)
add_subdirectory(unit/model)
add_subdirectory(unit/render)
add_subdirectory(unit/agent)
add_subdirectory(unit/app)

add_test(
  NAME architecture_guards
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_architecture.sh
)

add_test(NAME grapple_cli_smoke COMMAND grapple_cli)
add_test(NAME grapple_cli_render_plan_json COMMAND grapple_cli --render-plan-json)
add_test(NAME grapple_cli_preview_frame COMMAND grapple_cli --preview-frame)
add_test(NAME grapple_cli_export_smoke COMMAND grapple_cli --export-smoke)
add_test(NAME grapple_cli_save_package COMMAND grapple_cli --save-package)
