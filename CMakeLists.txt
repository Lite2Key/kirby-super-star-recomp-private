cmake_minimum_required(VERSION 3.25)
project(kss_recomp VERSION 0.1.0 LANGUAGES CXX)

option(KSS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_library(kss_runtime
    src/runtime/address_space.cpp
    src/runtime/alu.cpp
    src/runtime/bootstrap.cpp
    src/runtime/deterministic_scheduler.cpp
    src/runtime/dispatcher.cpp
    src/runtime/dual_bus.cpp
    src/runtime/generated_block_runner.cpp
    src/runtime/lifted_execution.cpp
    src/runtime/rom_validation.cpp
    src/runtime/sa1_timing.cpp
    src/recompiled/reset_blocks.cpp
)
target_include_directories(kss_runtime PUBLIC include)
target_compile_features(kss_runtime PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(kss_runtime PRIVATE /W4 $<$<BOOL:${KSS_WARNINGS_AS_ERRORS}>:/WX> /permissive-)
else()
    target_compile_options(kss_runtime PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${KSS_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

add_executable(kss-recomp src/runtime/main.cpp)
target_link_libraries(kss-recomp PRIVATE kss_runtime)

include(CTest)
if(BUILD_TESTING)
    add_executable(kss-runtime-tests tests/runtime/runtime_tests.cpp)
    target_link_libraries(kss-runtime-tests PRIVATE kss_runtime)
    add_test(NAME runtime-foundation COMMAND kss-runtime-tests)

    add_executable(kss-lifted-tests tests/runtime/lifted_execution_tests.cpp)
    target_link_libraries(kss-lifted-tests PRIVATE kss_runtime)
    add_test(NAME lifted-semantics COMMAND kss-lifted-tests)

    add_executable(kss-sa1-timing-tests tests/runtime/sa1_timing_tests.cpp)
    target_link_libraries(kss-sa1-timing-tests PRIVATE kss_runtime)
    add_test(NAME sa1-reset-timing COMMAND kss-sa1-timing-tests)

    add_executable(kss-generated-block-tests tests/runtime/generated_block_tests.cpp)
    target_link_libraries(kss-generated-block-tests PRIVATE kss_runtime)
    add_test(NAME generated-block-dispatch COMMAND kss-generated-block-tests)

    add_executable(kss-mvn-tests tests/runtime/mvn_execution_tests.cpp)
    target_link_libraries(kss-mvn-tests PRIVATE kss_runtime)
    add_test(NAME mvn-semantics COMMAND kss-mvn-tests)
endif()
