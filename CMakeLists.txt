cmake_minimum_required(VERSION 3.20)
project(gateway_port VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
option(GATEWAY_WITH_SDL "Build the SDL3 host diagnostic" ON)
option(GATEWAY_WITH_UNICORN "Build the real-mode startup probe" ON)
add_library(nuked_opl3 STATIC third_party/nuked-opl3/opl3.c)
target_include_directories(nuked_opl3 PUBLIC third_party/nuked-opl3)
add_library(gateway_core src/mz.cpp src/rtlink.cpp src/object_flags.cpp src/story_model.cpp src/dos_clock.cpp src/dos_files.cpp src/dos_fcb.cpp src/dos_memory.cpp src/vga.cpp src/opl.cpp src/adlib_driver.cpp src/pit.cpp src/pic.cpp src/crusher.cpp src/png.cpp src/regions.cpp src/debug_scene.cpp src/debug_state.cpp src/debug_ui.cpp)
target_include_directories(gateway_core PUBLIC src)
target_link_libraries(gateway_core PUBLIC nuked_opl3)
target_compile_options(gateway_core PRIVATE -Wall -Wextra -Wpedantic)
add_executable(gateway-inspect src/inspect.cpp)
target_link_libraries(gateway-inspect PRIVATE gateway_core)
add_executable(gateway-overlay src/overlay.cpp)
target_link_libraries(gateway-overlay PRIVATE gateway_core)
add_executable(gateway-extract src/extract.cpp)
target_link_libraries(gateway-extract PRIVATE gateway_core)
add_executable(gateway-story-check tools/check_action_matrix.cpp)
target_link_libraries(gateway-story-check PRIVATE gateway_core)
if(GATEWAY_WITH_SDL)
  find_package(SDL3 CONFIG REQUIRED)
  add_executable(gateway-host src/host.cpp)
  target_link_libraries(gateway-host PRIVATE SDL3::SDL3)
  if(BUILD_TESTING)
    add_executable(sdl-input-tests tests/sdl_input_tests.cpp src/sdl_host.cpp)
    target_link_libraries(sdl-input-tests PRIVATE gateway_core SDL3::SDL3)
    add_test(NAME sdl-input COMMAND sdl-input-tests)
    set_tests_properties(sdl-input PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 10)
    add_test(NAME sdl-host COMMAND gateway-host --smoke-test)
    set_tests_properties(sdl-host PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 10)
  endif()
  install(TARGETS gateway-host RUNTIME DESTINATION bin)
endif()
if(BUILD_TESTING)
  add_executable(mouse-cursor-tests tests/mouse_cursor_tests.cpp)
  target_link_libraries(mouse-cursor-tests PRIVATE gateway_core)
  add_test(NAME mouse-cursor COMMAND mouse-cursor-tests)
  add_executable(bios-text-tests tests/bios_text_tests.cpp)
  target_link_libraries(bios-text-tests PRIVATE gateway_core)
  add_test(NAME bios-text COMMAND bios-text-tests)
  add_executable(rtlink-tests tests/rtlink_tests.cpp)
  target_link_libraries(rtlink-tests PRIVATE gateway_core)
  add_test(NAME rtlink COMMAND rtlink-tests)
  add_executable(object-flags-tests tests/object_flags_tests.cpp)
  target_link_libraries(object-flags-tests PRIVATE gateway_core)
  add_test(NAME object-flags COMMAND object-flags-tests)
  add_executable(story-model-tests tests/story_model_tests.cpp)
  target_link_libraries(story-model-tests PRIVATE gateway_core)
  add_test(NAME story-model COMMAND story-model-tests)
  add_executable(story-child-link-tests tests/story_child_link_tests.cpp)
  target_link_libraries(story-child-link-tests PRIVATE gateway_core)
  add_test(NAME story-child-links COMMAND story-child-link-tests)
  add_test(NAME story-action-matrix COMMAND gateway-story-check
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/gateway-action-matrix.md)
  add_test(NAME story-action-admission COMMAND gateway-story-check
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/gateway-action-admission.md)
  add_executable(dos-files-tests tests/dos_files_tests.cpp)
  target_link_libraries(dos-files-tests PRIVATE gateway_core)
  add_test(NAME dos-files COMMAND dos-files-tests)
  add_executable(dos-fcb-tests tests/dos_fcb_tests.cpp)
  target_link_libraries(dos-fcb-tests PRIVATE gateway_core)
  add_test(NAME dos-fcb COMMAND dos-fcb-tests)
  add_executable(dos-clock-tests tests/dos_clock_tests.cpp)
  target_link_libraries(dos-clock-tests PRIVATE gateway_core)
  add_test(NAME dos-clock COMMAND dos-clock-tests)
  add_executable(pit-tests tests/pit_tests.cpp)
  target_link_libraries(pit-tests PRIVATE gateway_core)
  add_test(NAME pit COMMAND pit-tests)
  add_executable(opl-tests tests/opl_tests.cpp)
  target_link_libraries(opl-tests PRIVATE gateway_core)
  add_test(NAME opl COMMAND opl-tests)
  add_executable(adlib-driver-tests tests/adlib_driver_tests.cpp)
  target_link_libraries(adlib-driver-tests PRIVATE gateway_core)
  add_test(NAME adlib-driver COMMAND adlib-driver-tests)
  add_executable(vga-tests tests/vga_tests.cpp)
  target_link_libraries(vga-tests PRIVATE gateway_core)
  add_test(NAME vga COMMAND vga-tests)
  add_executable(dos-memory-tests tests/dos_memory_tests.cpp)
  target_link_libraries(dos-memory-tests PRIVATE gateway_core)
  add_test(NAME dos-memory COMMAND dos-memory-tests)
  add_executable(mz-tests tests/mz_tests.cpp)
  target_link_libraries(mz-tests PRIVATE gateway_core)
  add_test(NAME mz-loader COMMAND mz-tests)
  add_executable(pic-tests tests/pic_tests.cpp)
  target_link_libraries(pic-tests PRIVATE gateway_core)
  add_test(NAME pic COMMAND pic-tests)
  add_executable(regions-tests tests/regions_tests.cpp)
  target_link_libraries(regions-tests PRIVATE gateway_core)
  add_test(NAME regions COMMAND regions-tests)
  add_executable(debug-scene-tests tests/debug_scene_tests.cpp)
  target_link_libraries(debug-scene-tests PRIVATE gateway_core)
  add_test(NAME debug-scene COMMAND debug-scene-tests)
  add_executable(debug-state-tests tests/debug_state_tests.cpp)
  target_link_libraries(debug-state-tests PRIVATE gateway_core)
  add_test(NAME debug-state COMMAND debug-state-tests)
  add_executable(debug-ui-tests tests/debug_ui_tests.cpp)
  target_link_libraries(debug-ui-tests PRIVATE gateway_core)
  add_test(NAME debug-ui COMMAND debug-ui-tests)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME pic-extract COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_pic_tests.py $<TARGET_FILE:gateway-extract>)
    add_test(NAME story-disassembly COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/disassemble_story_tests.py)
    add_test(NAME story-admission-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/summarize_admission_trace_tests.py)
    add_test(NAME story-visibility-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/visibility_trace_tests.py)
    add_test(NAME story-visibility-correlation COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/visibility_correlation_tests.py)
    add_test(NAME story-object-write-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/object_write_trace_tests.py)
    add_test(NAME story-navigation-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/navigation_trace_tests.py)
    add_test(NAME story-room-exits COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/room_exit_inspect_tests.py)
    add_test(NAME story-puzzle-schedule COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/story_scenarios_tests.py)
    add_test(NAME beach-pin-state COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_beach_pin_tests.py)
    add_test(NAME tanning-up-callback COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_tanning_up_tests.py)
    add_test(NAME vr-up-callback COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_vr_up_tests.py)
    add_test(NAME study-up-callback COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_study_up_tests.py)
    add_test(NAME study-script-inspect COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_study_script_tests.py)
    add_test(NAME study-therapy-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_study_therapy_tests.py)
    add_test(NAME study-action-match-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_study_action_match_tests.py)
    add_test(NAME study-response-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_study_response_tests.py)
    add_test(NAME vr-return-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_vr_return_tests.py)
    add_test(NAME resident-room-transfer COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_room_transfer_tests.py)
    add_test(NAME ghidra-import-plan COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/prepare_ghidra_tests.py)
    add_test(NAME decomp-tracker COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/decomp_status_tests.py)
    add_test(NAME classify-shapes COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/classify_shapes_tests.py)
    add_test(NAME room-callback-inspect COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/room_callback_inspect_tests.py)
    add_test(NAME room-callback-compare COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/room_callback_compare_tests.py)
    add_test(NAME native-handler-inventory COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_handler_inventory_tests.py)
    add_test(NAME native-callback-coverage COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/audit_callback_coverage_tests.py)
    add_test(NAME native-handler-evidence COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/handler_evidence_tests.py)
    add_test(NAME story-child-chain COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/child_chain_inspect_tests.py)
    add_test(NAME story-world-export COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/story_world_export_tests.py)
    add_test(NAME object-state-export COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/object_state_export_tests.py)
    add_test(NAME story-replay-driver COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/trace_game_logic_tests.py)
    add_test(NAME walkthrough-replay-plan COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_walkthrough_tests.py)
    add_test(NAME gateway-save-inspect COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_gateway_save_tests.py)
    add_test(NAME story-action-batch COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_action_matrix_tests.py)
  endif()
endif()
install(TARGETS gateway-inspect gateway-extract gateway-overlay gateway-story-check RUNTIME DESTINATION bin)

if(GATEWAY_WITH_UNICORN)
  find_path(UNICORN_INCLUDE_DIR unicorn/unicorn.h REQUIRED)
  find_library(UNICORN_LIBRARY unicorn REQUIRED)
  add_executable(gateway-probe src/probe.cpp)
  target_include_directories(gateway-probe PRIVATE ${UNICORN_INCLUDE_DIR})
  target_link_libraries(gateway-probe PRIVATE gateway_core ${UNICORN_LIBRARY})
  if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME rtlink-tool COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/overlay_tool_tests.py $<TARGET_FILE:gateway-overlay>)
    add_test(NAME real-mode-probe COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/probe_tests.py $<TARGET_FILE:gateway-probe>)
  endif()
  if(GATEWAY_WITH_SDL)
    add_executable(gateway src/probe.cpp src/sdl_host.cpp)
    target_compile_definitions(gateway PRIVATE GATEWAY_SDL=1)
    target_include_directories(gateway PRIVATE ${UNICORN_INCLUDE_DIR})
    target_link_libraries(gateway PRIVATE gateway_core ${UNICORN_LIBRARY} SDL3::SDL3)
    if(BUILD_TESTING)
      add_test(NAME sdl-runtime COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/sdl_runtime_test.py $<TARGET_FILE:gateway>)
      set_tests_properties(sdl-runtime PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 20)
    endif()
    install(TARGETS gateway RUNTIME DESTINATION bin)
  endif()
  install(TARGETS gateway-probe RUNTIME DESTINATION bin)
endif()
