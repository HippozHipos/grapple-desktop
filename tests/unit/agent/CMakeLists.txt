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

add_executable(grapple_agent_conversation_tests
  AgentConversationStateTest.cpp
)

target_link_libraries(grapple_agent_conversation_tests
  PRIVATE
    Grapple::Agent
)

target_include_directories(grapple_agent_conversation_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_agent_conversation_tests COMMAND grapple_agent_conversation_tests)

add_executable(grapple_agent_bridge_tests
  AgentBridgeTest.cpp
)

target_link_libraries(grapple_agent_bridge_tests
  PRIVATE
    Grapple::Agent
)

target_include_directories(grapple_agent_bridge_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
)

add_test(NAME grapple_agent_bridge_tests COMMAND grapple_agent_bridge_tests)
