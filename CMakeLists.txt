cmake_minimum_required(VERSION 3.25)
project(kss_recomp VERSION 0.1.0 LANGUAGES CXX)

option(KSS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(KSS_USE_PRIVATE_GENERATED "Compile ROM-derived generated blocks from the ignored private tree" OFF)
set(KSS_PRIVATE_GENERATED_ROOT "${CMAKE_SOURCE_DIR}/.private/generated/first-frame" CACHE PATH
    "Private static-recompiler output root")

add_library(kss_runtime
    src/runtime/address_space.cpp
    src/runtime/alu.cpp
    src/runtime/bootstrap.cpp
    src/runtime/boot_probe.cpp
    src/runtime/controller.cpp
    src/runtime/deterministic_scheduler.cpp
    src/runtime/dispatcher.cpp
    src/runtime/dual_bus.cpp
    src/runtime/dsp_brr_decoder.cpp
    src/runtime/dsp_core.cpp
    src/runtime/dsp_echo_fir.cpp
    src/runtime/dsp_stereo_mixer.cpp
    src/runtime/dsp_voice_pipeline.cpp
    src/runtime/generated_block_runner.cpp
    src/runtime/lifted_execution.cpp
    src/runtime/multi_clock_coordinator.cpp
    src/runtime/native_host.cpp
    src/runtime/rom_validation.cpp
    src/runtime/runtime_event_chain.cpp
    src/runtime/save_ram.cpp
    src/runtime/sa1_frame_domain.cpp
    src/runtime/sa1_timing.cpp
    src/runtime/sa1_registers.cpp
    src/runtime/scpu_micro_access_recorder.cpp
    src/runtime/snes_registers.cpp
    src/runtime/spc700.cpp
    src/runtime/visible_frame_capture.cpp
    src/recompiled/reset_blocks.cpp
    src/recompiled/first_frame_blocks.cpp
)
target_include_directories(kss_runtime PUBLIC include)
target_compile_features(kss_runtime PUBLIC cxx_std_20)

if(KSS_USE_PRIVATE_GENERATED)
    set(KSS_PRIVATE_GENERATED_SOURCE "${KSS_PRIVATE_GENERATED_ROOT}/src/private_first_frame_blocks.cpp")
    set(KSS_PRIVATE_GENERATED_HEADER
        "${KSS_PRIVATE_GENERATED_ROOT}/include/kss/generated_private_first_frame_blocks.hpp")
    if(NOT EXISTS "${KSS_PRIVATE_GENERATED_SOURCE}" OR NOT EXISTS "${KSS_PRIVATE_GENERATED_HEADER}")
        message(FATAL_ERROR
            "Private generated blocks are missing. Run prepare-private-recomp.cmd before configuring.")
    endif()
    target_sources(kss_runtime PRIVATE "${KSS_PRIVATE_GENERATED_SOURCE}")
    target_include_directories(kss_runtime PRIVATE "${KSS_PRIVATE_GENERATED_ROOT}/include")
    target_compile_definitions(kss_runtime PRIVATE KSS_USE_PRIVATE_GENERATED=1)
endif()

if(MSVC)
    target_compile_options(kss_runtime PRIVATE /W4 $<$<BOOL:${KSS_WARNINGS_AS_ERRORS}>:/WX> /permissive-)
else()
    target_compile_options(kss_runtime PRIVATE -Wall -Wextra -Wpedantic $<$<BOOL:${KSS_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

add_executable(kss-recomp src/runtime/main.cpp)
target_link_libraries(kss-recomp PRIVATE kss_runtime)

if(WIN32)
    add_executable(kss-native
        src/platform/win32_main.cpp
        src/platform/win32_host.cpp)
    target_link_libraries(kss-native PRIVATE kss_runtime user32 gdi32)
    if(MSVC)
        target_compile_options(kss-native PRIVATE /W4
            $<$<BOOL:${KSS_WARNINGS_AS_ERRORS}>:/WX> /permissive-)
    endif()
endif()

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

    add_executable(kss-sa1-frame-domain-tests tests/runtime/sa1_frame_domain_tests.cpp)
    target_link_libraries(kss-sa1-frame-domain-tests PRIVATE kss_runtime)
    add_test(NAME sa1-first-frame-domain COMMAND kss-sa1-frame-domain-tests)

    add_executable(kss-generated-block-tests tests/runtime/generated_block_tests.cpp)
    target_link_libraries(kss-generated-block-tests PRIVATE kss_runtime)
    add_test(NAME generated-block-dispatch COMMAND kss-generated-block-tests)

    add_executable(kss-mvn-tests tests/runtime/mvn_execution_tests.cpp)
    target_link_libraries(kss-mvn-tests PRIVATE kss_runtime)
    add_test(NAME mvn-semantics COMMAND kss-mvn-tests)

    add_executable(kss-snes-dma-tests tests/runtime/snes_dma_tests.cpp)
    target_link_libraries(kss-snes-dma-tests PRIVATE kss_runtime)
    add_test(NAME snes-dma-registers COMMAND kss-snes-dma-tests)

    add_executable(kss-hardware-register-tests tests/runtime/hardware_register_tests.cpp)
    target_link_libraries(kss-hardware-register-tests PRIVATE kss_runtime)
    add_test(NAME first-frame-hardware-registers COMMAND kss-hardware-register-tests)

    add_executable(kss-spc700-tests tests/runtime/spc700_tests.cpp)
    target_link_libraries(kss-spc700-tests PRIVATE kss_runtime)
    add_test(NAME spc700-observed-slice COMMAND kss-spc700-tests)

    add_executable(kss-spc-bus-integration-tests tests/runtime/spc_bus_integration_tests.cpp)
    target_link_libraries(kss-spc-bus-integration-tests PRIVATE kss_runtime)
    add_test(NAME spc700-hardware-boundary COMMAND kss-spc-bus-integration-tests)

    add_executable(kss-multi-clock-tests tests/runtime/multi_clock_coordinator_tests.cpp)
    target_link_libraries(kss-multi-clock-tests PRIVATE kss_runtime)
    add_test(NAME multi-clock-coordinator COMMAND kss-multi-clock-tests)

    add_executable(kss-scpu-access-recorder-tests tests/runtime/scpu_micro_access_recorder_tests.cpp)
    target_link_libraries(kss-scpu-access-recorder-tests PRIVATE kss_runtime)
    add_test(NAME scpu-micro-access-recorder COMMAND kss-scpu-access-recorder-tests)

    add_executable(kss-boot-probe-clock-tests tests/runtime/boot_probe_clock_tests.cpp)
    target_link_libraries(kss-boot-probe-clock-tests PRIVATE kss_runtime)
    add_test(NAME boot-probe-clock-integration COMMAND kss-boot-probe-clock-tests)

    add_executable(kss-save-ram-tests tests/runtime/save_ram_tests.cpp)
    target_link_libraries(kss-save-ram-tests PRIVATE kss_runtime)
    add_test(NAME save-ram-persistence COMMAND kss-save-ram-tests)

    add_executable(kss-controller-tests tests/runtime/controller_tests.cpp)
    target_link_libraries(kss-controller-tests PRIVATE kss_runtime)
    add_test(NAME snes-controller-serial COMMAND kss-controller-tests)

    add_executable(kss-native-host-tests tests/runtime/native_host_tests.cpp)
    target_link_libraries(kss-native-host-tests PRIVATE kss_runtime)
    add_test(NAME native-host-boundary COMMAND kss-native-host-tests)

    add_executable(kss-dsp-brr-decoder-tests tests/runtime/dsp_brr_decoder_tests.cpp)
    target_link_libraries(kss-dsp-brr-decoder-tests PRIVATE kss_runtime)
    add_test(NAME snes-dsp-brr-decoder COMMAND kss-dsp-brr-decoder-tests)

    add_executable(kss-dsp-voice-tests tests/runtime/dsp_voice_pipeline_tests.cpp)
    target_link_libraries(kss-dsp-voice-tests PRIVATE kss_runtime)
    add_test(NAME snes-dsp-voice-pipeline COMMAND kss-dsp-voice-tests)

    add_executable(kss-dsp-mixer-tests tests/runtime/dsp_stereo_mixer_tests.cpp)
    target_link_libraries(kss-dsp-mixer-tests PRIVATE kss_runtime)
    add_test(NAME snes-dsp-stereo-mixer COMMAND kss-dsp-mixer-tests)

    add_executable(kss-dsp-echo-tests tests/runtime/dsp_echo_fir_tests.cpp)
    target_link_libraries(kss-dsp-echo-tests PRIVATE kss_runtime)
    add_test(NAME snes-dsp-echo-fir COMMAND kss-dsp-echo-tests)

    add_executable(kss-dsp-core-tests tests/runtime/dsp_core_integration_tests.cpp)
    target_link_libraries(kss-dsp-core-tests PRIVATE kss_runtime)
    add_test(NAME snes-dsp-core-integration COMMAND kss-dsp-core-tests)

    add_executable(kss-visible-frame-capture-tests
        tests/runtime/visible_frame_capture_tests.cpp)
    target_link_libraries(kss-visible-frame-capture-tests PRIVATE kss_runtime)
    add_test(NAME visible-frame-boundary-capture COMMAND kss-visible-frame-capture-tests)

    add_executable(kss-runtime-event-chain-tests
        tests/runtime/runtime_event_chain_tests.cpp)
    target_link_libraries(kss-runtime-event-chain-tests PRIVATE kss_runtime)
    add_test(NAME runtime-event-chain COMMAND kss-runtime-event-chain-tests)
endif()
