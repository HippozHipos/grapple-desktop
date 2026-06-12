add_executable(grapple_agent_tests
  AgentToolRegistryTest.cpp
)

target_link_libraries(grapple_agent_tests
  PRIVATE
    Grapple::Agent
)

target_include_directories(grapple_agent_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_agent_tests COMMAND grapple_agent_tests)

