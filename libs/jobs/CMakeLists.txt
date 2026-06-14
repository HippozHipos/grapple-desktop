find_package(Threads REQUIRED)

add_library(grapple_jobs
  src/CancellationToken.cpp
  src/JobScheduler.cpp
  src/JobQueue.cpp
  src/MainThreadDispatcher.cpp
  src/ProjectCommandQueue.cpp
)

add_library(Grapple::Jobs ALIAS grapple_jobs)

target_include_directories(grapple_jobs
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(grapple_jobs
  PUBLIC
    Grapple::Foundation
    Grapple::Project
    Threads::Threads
)
