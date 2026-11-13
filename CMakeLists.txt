cmake_minimum_required(VERSION 3.16)
project(cpu_scheduler_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(scheduler_core
    src/simulator.cpp
    src/scheduler.cpp
    src/deadlock_manager.cpp
)
target_include_directories(scheduler_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

function(enable_project_warnings target)
if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /O2)
else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -O2)
endif()
endfunction()
enable_project_warnings(scheduler_core)

add_executable(scheduler_sim src/main.cpp)
target_link_libraries(scheduler_sim PRIVATE scheduler_core)
enable_project_warnings(scheduler_sim)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_invariants tests/test_invariants.cpp)
    target_link_libraries(test_invariants PRIVATE scheduler_core)
    enable_project_warnings(test_invariants)
    add_test(NAME scheduler_invariants COMMAND test_invariants)
endif()
