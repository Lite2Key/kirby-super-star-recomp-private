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
    src/runtime/rom_validation.cpp
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
endif()
