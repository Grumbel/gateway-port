cmake_minimum_required(VERSION 3.20)
project(gateway_port VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
option(GATEWAY_WITH_SDL "Build the SDL3 host diagnostic" ON)
option(GATEWAY_WITH_UNICORN "Build the real-mode startup probe" ON)
option(GATEWAY_INTRO_DECOMPILE_ONLY "Build only the reconstructed original GATE intro C library and tests" OFF)

# Playable caption-only component from original source-shaped C. Keeping
# these translation units separate allows dead original service branches to
# be discarded without a fake implementation of their ABI.
set(GATEWAY_INTRO_SOURCE_C_CAPTION_SOURCES
  decomp/launcher/gate_exe.c decomp/launcher/gate_exe_intro_setup.c
  decomp/launcher/gate_exe_helpers.c decomp/launcher/gate_c_runtime_string.c
  decomp/launcher/gate_rtl_font.c decomp/launcher/gate_rtl.c
  decomp/launcher/gate_rtl_draw.c decomp/launcher/gate_rtl_linkage.c
  decomp/launcher/gate_rtl_file.c decomp/launcher/gate_dos_file_runtime.c
  src/intro_dos_host_files.c src/intro_host_memory.c
  src/intro_host_rtl_font.c src/intro_host_rtl_text_globals.c
  src/intro_host_rtl_pixel.c src/intro_host_input.c
  src/intro_source_c_captions.c)

add_executable(gateway-intro-source-c-dump
  ${GATEWAY_INTRO_SOURCE_C_CAPTION_SOURCES}
  src/intro_source_c_dump.c)
target_include_directories(gateway-intro-source-c-dump PRIVATE decomp/launcher src)
target_compile_options(gateway-intro-source-c-dump PRIVATE
  -ffunction-sections -fdata-sections)
target_link_options(gateway-intro-source-c-dump PRIVATE -Wl,--gc-sections)
install(TARGETS gateway-intro-source-c-dump RUNTIME DESTINATION bin)

# The original mode-13h source-C image loader and draw path, with only the
# compressed DCL payload decoder and resident VGA memory operations in host
# code. Kept separate from the caption player until EXE:1302 sequencing is
# verified against the original game.
set(GATEWAY_INTRO_SOURCE_C_PICTURE_SOURCES
  decomp/launcher/gate_rtl_file.c
  decomp/launcher/gate_rtl_file_linkage.c
  decomp/launcher/gate_rtl_resource.c
  decomp/launcher/gate_rtl_loader.c
  decomp/launcher/gate_rtl_image.c
  decomp/launcher/gate_rtl_image_prepare.c
  decomp/launcher/gate_rtl_palette.c
  decomp/launcher/gate_rtl_image_finish.c
  decomp/launcher/gate_rtl_image_processor.c
  decomp/launcher/gate_vfx_image.c
  decomp/launcher/gate_dos_file_runtime.c
  src/intro_dos_host_files.c src/intro_host_memory.c src/intro_host_vfx.c
  src/intro_host_rtl_globals.c
  src/intro_host_rtl_palette_globals.c src/intro_host_vga_ports.c
  src/intro_source_c_picture_dump.c)

set(GATEWAY_INTRO_SOURCE_C_COMPOSITE_SOURCES
  ${GATEWAY_INTRO_SOURCE_C_CAPTION_SOURCES}
  ${GATEWAY_INTRO_SOURCE_C_PICTURE_SOURCES}
  decomp/launcher/gate_input.c
  decomp/launcher/gate_c_runtime_random.c
  decomp/launcher/gate_rtl_audio_startup.c
  decomp/launcher/gate_rtl_mode.c
  decomp/launcher/gate_rtl_graphics.c
  decomp/launcher/gate_rtl_intro_animation.c
  decomp/launcher/gate_rtl_intro_sequence.c
  src/intro_host_rtl_graphics_globals.c
  src/intro_host_video_mode.c
  src/intro_source_c_bootstrap.c
  src/intro_source_c_composite.c)
list(REMOVE_ITEM GATEWAY_INTRO_SOURCE_C_COMPOSITE_SOURCES
  src/intro_source_c_captions.c src/intro_source_c_picture_dump.c)
list(REMOVE_DUPLICATES GATEWAY_INTRO_SOURCE_C_COMPOSITE_SOURCES)

# This is reconstructed original C, not the separate SDL/C++ approximation
# in src/intro_player.cpp. It deliberately keeps undefined original runtime
# services as unresolved externals in the archive until they are recovered.
add_library(gateway_intro_decompiled STATIC
  decomp/launcher/gate_exe.c
  decomp/launcher/gate_exe_intro_setup.c
  decomp/launcher/gate_rtl_audio_startup.c
  decomp/launcher/gate_exe_helpers.c
  decomp/launcher/gate_input.c
  decomp/launcher/gate_c_runtime_string.c
  decomp/launcher/gate_c_runtime_random.c
  decomp/launcher/gate_c_runtime_clock.c
  decomp/launcher/gate_rtl_font.c
  decomp/launcher/gate_rtl.c
  decomp/launcher/gate_rtl_video.c
  decomp/launcher/gate_rtl_graphics.c
  decomp/launcher/gate_rtl_palette.c
  decomp/launcher/gate_rtl_mode.c
  decomp/launcher/gate_rtl_draw.c
  decomp/launcher/gate_rtl_image.c
  decomp/launcher/gate_rtl_file.c
  decomp/launcher/gate_rtl_file_linkage.c
  decomp/launcher/gate_vfx_image.c
  decomp/launcher/gate_dos_file_runtime.c
  decomp/launcher/gate_rtl_loader.c
  decomp/launcher/gate_rtl_resource.c
  decomp/launcher/gate_rtl_image_finish.c
  decomp/launcher/gate_rtl_image_prepare.c
  decomp/launcher/gate_rtl_image_processor.c
  decomp/launcher/gate_rtl_intro_animation.c
  decomp/launcher/gate_rtl_intro_sequence.c
  decomp/launcher/gate_rtl_linkage.c)
target_include_directories(gateway_intro_decompiled PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/decomp/launcher>
  $<INSTALL_INTERFACE:include/gateway/intro>)
target_compile_features(gateway_intro_decompiled PUBLIC c_std_11)
target_compile_options(gateway_intro_decompiled PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS gateway_intro_decompiled ARCHIVE DESTINATION lib)
add_library(gateway_intro_file_host STATIC src/intro_dos_host_files.c)
target_include_directories(gateway_intro_file_host PUBLIC decomp/launcher)
target_compile_features(gateway_intro_file_host PUBLIC c_std_11)
target_compile_options(gateway_intro_file_host PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS gateway_intro_file_host ARCHIVE DESTINATION lib)
add_library(gateway_intro_memory_host STATIC
  src/intro_host_memory.c src/intro_host_rtl_font.c
  src/intro_host_rtl_globals.c
  src/intro_host_rtl_text_globals.c src/intro_host_rtl_graphics_globals.c
  src/intro_host_rtl_pixel.c src/intro_host_rtl_palette_globals.c)
target_include_directories(gateway_intro_memory_host PUBLIC decomp/launcher)
target_compile_features(gateway_intro_memory_host PUBLIC c_std_11)
target_compile_options(gateway_intro_memory_host PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS gateway_intro_memory_host ARCHIVE DESTINATION lib)
add_executable(launcher-intro-input-dependencies-tests
  tests/launcher_intro_input_dependencies_tests.c)
target_link_libraries(launcher-intro-input-dependencies-tests PRIVATE
  gateway_intro_memory_host)
add_test(NAME launcher-intro-input-dependencies
  COMMAND launcher-intro-input-dependencies-tests)
add_library(gateway_intro_vga_ports_host STATIC src/intro_host_vga_ports.c)
target_include_directories(gateway_intro_vga_ports_host PUBLIC decomp/launcher)
target_compile_features(gateway_intro_vga_ports_host PUBLIC c_std_11)
target_compile_options(gateway_intro_vga_ports_host PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS gateway_intro_vga_ports_host ARCHIVE DESTINATION lib)
add_library(gateway_intro_video_mode_host STATIC
  src/intro_host_video_mode.c src/intro_host_video_clear.c)
target_include_directories(gateway_intro_video_mode_host PUBLIC decomp/launcher)
target_compile_features(gateway_intro_video_mode_host PUBLIC c_std_11)
target_compile_options(gateway_intro_video_mode_host PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(gateway_intro_video_mode_host PUBLIC gateway_intro_memory_host)
install(TARGETS gateway_intro_video_mode_host ARCHIVE DESTINATION lib)
add_library(gateway_intro_input_host STATIC src/intro_host_input.c)
target_include_directories(gateway_intro_input_host PUBLIC decomp/launcher)
target_compile_features(gateway_intro_input_host PUBLIC c_std_11)
target_compile_options(gateway_intro_input_host PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS gateway_intro_input_host ARCHIVE DESTINATION lib)
add_library(gateway_intro_vfx_host STATIC src/intro_host_vfx.c)
target_include_directories(gateway_intro_vfx_host PUBLIC decomp/launcher)
target_compile_features(gateway_intro_vfx_host PUBLIC c_std_11)
target_compile_options(gateway_intro_vfx_host PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS gateway_intro_vfx_host ARCHIVE DESTINATION lib)
add_library(gateway_intro_pic_host STATIC
  src/intro_host_pic_decode.cpp src/pic.cpp src/crusher.cpp)
target_include_directories(gateway_intro_pic_host PUBLIC
  decomp/launcher src)
target_compile_features(gateway_intro_pic_host PUBLIC cxx_std_20)
target_compile_options(gateway_intro_pic_host PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS gateway_intro_pic_host ARCHIVE DESTINATION lib)
add_executable(gateway-intro-source-c-picture-dump
  ${GATEWAY_INTRO_SOURCE_C_PICTURE_SOURCES})
target_include_directories(gateway-intro-source-c-picture-dump PRIVATE
  decomp/launcher src)
target_compile_options(gateway-intro-source-c-picture-dump PRIVATE
  -ffunction-sections -fdata-sections)
target_link_options(gateway-intro-source-c-picture-dump PRIVATE -Wl,--gc-sections)
target_link_libraries(gateway-intro-source-c-picture-dump PRIVATE
  gateway_intro_pic_host)
install(TARGETS gateway-intro-source-c-picture-dump RUNTIME DESTINATION bin)
add_executable(gateway-intro-source-c-composite
  ${GATEWAY_INTRO_SOURCE_C_COMPOSITE_SOURCES})
target_include_directories(gateway-intro-source-c-composite PRIVATE decomp/launcher src)
target_compile_options(gateway-intro-source-c-composite PRIVATE
  -ffunction-sections -fdata-sections)
target_link_options(gateway-intro-source-c-composite PRIVATE -Wl,--gc-sections)
target_link_libraries(gateway-intro-source-c-composite PRIVATE gateway_intro_pic_host)
install(TARGETS gateway-intro-source-c-composite RUNTIME DESTINATION bin)
add_executable(launcher-intro-source-c-live-tests
  ${GATEWAY_INTRO_SOURCE_C_COMPOSITE_SOURCES}
  tests/launcher_intro_source_c_live_tests.c)
target_include_directories(launcher-intro-source-c-live-tests PRIVATE decomp/launcher src)
target_compile_definitions(launcher-intro-source-c-live-tests PRIVATE
  GATE_SOURCE_C_LIBRARY)
target_compile_options(launcher-intro-source-c-live-tests PRIVATE
  -ffunction-sections -fdata-sections)
target_link_options(launcher-intro-source-c-live-tests PRIVATE -Wl,--gc-sections)
target_link_libraries(launcher-intro-source-c-live-tests PRIVATE gateway_intro_pic_host)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos/GATE.EXE" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos/GATE.RTL" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos/GATE_301.PIC")
  add_test(NAME launcher-intro-source-c-live-native COMMAND
    launcher-intro-source-c-live-tests --from-original
    ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
  add_test(NAME launcher-intro-source-c-live-original-logo COMMAND
    launcher-intro-source-c-live-tests --from-original
    ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos --original-video-start)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin" AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos/GATE_300.PIC")
  add_test(NAME launcher-intro-source-c-live-snapshot COMMAND
    launcher-intro-source-c-live-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin
    ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
endif()
install(FILES decomp/launcher/gate_exe.h decomp/launcher/gate_rtl.h
              decomp/launcher/gate_rtl_audio_startup.h
              decomp/launcher/gate_rtl_video.h
              decomp/launcher/gate_rtl_graphics.h
              decomp/launcher/gate_rtl_palette.h
              decomp/launcher/gate_rtl_mode.h
              decomp/launcher/gate_rtl_draw.h
              decomp/launcher/gate_rtl_image.h
              decomp/launcher/gate_rtl_file.h
              decomp/launcher/gate_dos_file_runtime.h
              decomp/launcher/gate_dos_host_files.h
              decomp/launcher/gate_intro_host_memory.h
              decomp/launcher/gate_intro_host_input.h
              decomp/launcher/gate_intro_host_vga_ports.h
              decomp/launcher/gate_intro_host_video_mode.h
        DESTINATION include/gateway/intro)

if(BUILD_TESTING)
  add_executable(launcher-intro-source-c-captions-tests
    ${GATEWAY_INTRO_SOURCE_C_CAPTION_SOURCES}
    tests/launcher_intro_source_c_captions_tests.c)
  target_include_directories(launcher-intro-source-c-captions-tests PRIVATE
    decomp/launcher src)
  target_compile_options(launcher-intro-source-c-captions-tests PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(launcher-intro-source-c-captions-tests PRIVATE
    -Wl,--gc-sections)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos/GATE_305.FNT")
    add_test(NAME launcher-intro-source-c-captions COMMAND
      launcher-intro-source-c-captions-tests
      ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin
      ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
  endif()
  add_executable(launcher-intro-audio-startup-tests
    decomp/launcher/gate_rtl_audio_startup.c
    tests/launcher_intro_audio_startup_tests.c)
  target_include_directories(launcher-intro-audio-startup-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-audio-startup COMMAND launcher-intro-audio-startup-tests)
  add_executable(launcher-decompile-tests
    decomp/launcher/gate_exe.c decomp/launcher/gate_exe_intro_setup.c
    decomp/launcher/gate_exe_helpers.c
    tests/launcher_decompile_tests.c)
  target_include_directories(launcher-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-decompile COMMAND launcher-decompile-tests)
  add_executable(launcher-rtl-decompile-tests
    decomp/launcher/gate_rtl.c decomp/launcher/gate_rtl_video.c
    decomp/launcher/gate_exe_helpers.c tests/launcher_rtl_decompile_tests.c)
  target_include_directories(launcher-rtl-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-decompile COMMAND launcher-rtl-decompile-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
  add_executable(launcher-rtl-font-decompile-tests
    decomp/launcher/gate_rtl_font.c
    tests/launcher_rtl_font_decompile_tests.c)
  target_include_directories(launcher-rtl-font-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-font-decompile COMMAND launcher-rtl-font-decompile-tests)
  add_executable(launcher-break-cleanup-decompile-tests
    decomp/launcher/dos/msc51_break_cleanup.c
    tests/launcher_break_cleanup_decompile_tests.c)
  target_include_directories(launcher-break-cleanup-decompile-tests PRIVATE decomp/launcher)
  target_compile_definitions(launcher-break-cleanup-decompile-tests PRIVATE GATE_BREAK_CLEANUP_HOST_TEST=1)
  add_test(NAME launcher-break-cleanup-decompile COMMAND launcher-break-cleanup-decompile-tests)
  add_executable(launcher-break-outer-decompile-tests
    decomp/launcher/dos/msc51_break_outer.c
    tests/launcher_break_outer_decompile_tests.c)
  target_compile_definitions(launcher-break-outer-decompile-tests PRIVATE GATE_BREAK_OUTER_HOST_TEST=1)
  add_test(NAME launcher-break-outer-decompile COMMAND launcher-break-outer-decompile-tests)
  add_executable(launcher-intro-native-font-tests
    decomp/launcher/gate_rtl_font.c decomp/launcher/gate_rtl.c
    decomp/launcher/gate_rtl_file.c
    decomp/launcher/gate_dos_file_runtime.c
    src/intro_dos_host_files.c src/intro_host_memory.c
    src/intro_host_rtl_font.c src/intro_host_rtl_text_globals.c
    tests/launcher_intro_native_font_tests.c)
  target_include_directories(launcher-intro-native-font-tests PRIVATE decomp/launcher)
  target_compile_options(launcher-intro-native-font-tests PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(launcher-intro-native-font-tests PRIVATE -Wl,--gc-sections)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos/GATE_305.FNT")
    add_test(NAME launcher-intro-native-original-font
      COMMAND launcher-intro-native-font-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
  endif()
  add_executable(launcher-intro-native-caption-tests
    decomp/launcher/gate_exe.c decomp/launcher/gate_exe_intro_setup.c
    decomp/launcher/gate_exe_helpers.c
    decomp/launcher/gate_c_runtime_string.c
    decomp/launcher/gate_rtl_font.c decomp/launcher/gate_rtl.c
    decomp/launcher/gate_rtl_draw.c decomp/launcher/gate_rtl_linkage.c
    decomp/launcher/gate_rtl_file.c
    decomp/launcher/gate_dos_file_runtime.c
    src/intro_dos_host_files.c src/intro_host_memory.c
    src/intro_host_rtl_font.c src/intro_host_rtl_text_globals.c
    src/intro_host_rtl_pixel.c src/intro_host_input.c
    tests/launcher_intro_native_caption_tests.c)
  target_include_directories(launcher-intro-native-caption-tests PRIVATE decomp/launcher)
  target_compile_options(launcher-intro-native-caption-tests PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(launcher-intro-native-caption-tests PRIVATE -Wl,--gc-sections)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos/GATE_305.FNT")
    add_test(NAME launcher-intro-native-original-caption
      COMMAND launcher-intro-native-caption-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
  endif()
  add_executable(launcher-rtl-video-decompile-tests
    decomp/launcher/gate_rtl_video.c decomp/launcher/gate_exe_helpers.c
    tests/launcher_rtl_video_decompile_tests.c)
  target_include_directories(launcher-rtl-video-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-video-decompile COMMAND launcher-rtl-video-decompile-tests)
  add_executable(launcher-intro-native-video-clear-tests
    decomp/launcher/gate_rtl_video.c src/intro_host_memory.c
    src/intro_host_video_mode.c src/intro_host_video_clear.c
    tests/launcher_intro_native_video_clear_tests.c)
  target_include_directories(launcher-intro-native-video-clear-tests PRIVATE decomp/launcher)
  target_compile_options(launcher-intro-native-video-clear-tests PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(launcher-intro-native-video-clear-tests PRIVATE -Wl,--gc-sections)
  add_test(NAME launcher-intro-native-video-clear COMMAND launcher-intro-native-video-clear-tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin")
    add_test(NAME launcher-intro-original-video-clear
      COMMAND launcher-intro-native-video-clear-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin)
  endif()
  add_executable(launcher-intro-keyboard-decompile-tests
    decomp/launcher/gate_input.c
    tests/launcher_intro_keyboard_decompile_tests.c)
  target_include_directories(launcher-intro-keyboard-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-keyboard-decompile
    COMMAND launcher-intro-keyboard-decompile-tests)
  add_executable(launcher-rtl-graphics-decompile-tests
    decomp/launcher/gate_rtl_graphics.c
    tests/launcher_rtl_graphics_decompile_tests.c)
  target_include_directories(launcher-rtl-graphics-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-graphics-decompile COMMAND launcher-rtl-graphics-decompile-tests)
  add_executable(launcher-rtl-palette-decompile-tests
    decomp/launcher/gate_rtl_palette.c
    tests/launcher_rtl_palette_decompile_tests.c)
  target_include_directories(launcher-rtl-palette-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-palette-decompile COMMAND launcher-rtl-palette-decompile-tests)
  add_executable(launcher-rtl-draw-decompile-tests
    decomp/launcher/gate_rtl_draw.c
    tests/launcher_rtl_draw_decompile_tests.c)
  target_include_directories(launcher-rtl-draw-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-draw-decompile COMMAND launcher-rtl-draw-decompile-tests)
  add_executable(launcher-rtl-image-decompile-tests
    decomp/launcher/gate_rtl_image.c
    tests/launcher_rtl_image_decompile_tests.c)
  target_include_directories(launcher-rtl-image-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-image-decompile COMMAND launcher-rtl-image-decompile-tests)
  add_executable(launcher-rtl-image-loader-decompile-tests
    decomp/launcher/gate_rtl_loader.c
    tests/launcher_rtl_image_loader_decompile_tests.c)
  target_include_directories(launcher-rtl-image-loader-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-image-loader-decompile COMMAND launcher-rtl-image-loader-decompile-tests)
  add_executable(launcher-rtl-image-resource-decompile-tests
    decomp/launcher/gate_rtl_resource.c
    tests/launcher_rtl_image_resource_decompile_tests.c)
  target_include_directories(launcher-rtl-image-resource-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-image-resource-decompile COMMAND launcher-rtl-image-resource-decompile-tests)
  add_executable(launcher-rtl-file-decompile-tests
    decomp/launcher/gate_rtl_file.c
    tests/launcher_rtl_file_decompile_tests.c)
  target_include_directories(launcher-rtl-file-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-file-decompile COMMAND launcher-rtl-file-decompile-tests)
  add_executable(launcher-dos-file-runtime-decompile-tests
    decomp/launcher/gate_dos_file_runtime.c
    tests/launcher_dos_file_runtime_decompile_tests.c)
  target_include_directories(launcher-dos-file-runtime-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-dos-file-runtime-decompile COMMAND launcher-dos-file-runtime-decompile-tests)
  add_executable(launcher-dos-host-files-tests
    decomp/launcher/gate_dos_file_runtime.c src/intro_dos_host_files.c
    tests/launcher_dos_host_files_tests.c)
  target_include_directories(launcher-dos-host-files-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-dos-host-files COMMAND launcher-dos-host-files-tests)
  add_executable(launcher-intro-host-memory-tests
    src/intro_host_memory.c tests/launcher_intro_host_memory_tests.c)
  target_include_directories(launcher-intro-host-memory-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-host-memory COMMAND launcher-intro-host-memory-tests)
  add_executable(launcher-intro-host-input-tests
    decomp/launcher/gate_input.c src/intro_host_memory.c
    src/intro_host_input.c tests/launcher_intro_host_input_tests.c)
  target_include_directories(launcher-intro-host-input-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-host-input COMMAND launcher-intro-host-input-tests)
  add_executable(launcher-intro-runtime-string-tests
    decomp/launcher/gate_c_runtime_string.c src/intro_host_memory.c
    tests/launcher_intro_runtime_string_tests.c)
  target_include_directories(launcher-intro-runtime-string-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-runtime-string COMMAND launcher-intro-runtime-string-tests)
  add_executable(launcher-intro-runtime-clock-tests
    decomp/launcher/gate_c_runtime_clock.c
    tests/launcher_intro_runtime_clock_tests.c)
  target_include_directories(launcher-intro-runtime-clock-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-runtime-clock COMMAND launcher-intro-runtime-clock-tests)
  add_executable(launcher-intro-original-entry-tests
    decomp/launcher/gate_exe_intro_setup.c src/intro_host_memory.c
    tests/launcher_intro_original_entry_tests.c)
  target_include_directories(launcher-intro-original-entry-tests PRIVATE decomp/launcher)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/intro-entry.bin" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/intro-setup-end.bin")
    add_test(NAME launcher-intro-original-entry COMMAND launcher-intro-original-entry-tests
      ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/intro-entry.bin
      ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/intro-setup-end.bin)
  endif()
  add_executable(launcher-intro-rtl-global-host-tests
    src/intro_host_memory.c src/intro_host_rtl_globals.c
    tests/launcher_intro_rtl_global_host_tests.c)
  target_include_directories(launcher-intro-rtl-global-host-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-rtl-global-host COMMAND launcher-intro-rtl-global-host-tests)
  add_executable(launcher-intro-rtl-graphics-global-host-tests
    src/intro_host_memory.c src/intro_host_rtl_globals.c
    src/intro_host_rtl_graphics_globals.c
    tests/launcher_intro_rtl_graphics_global_host_tests.c)
  target_include_directories(launcher-intro-rtl-graphics-global-host-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-rtl-graphics-global-host
    COMMAND launcher-intro-rtl-graphics-global-host-tests)
  add_executable(launcher-intro-rtl-text-host-tests
    src/intro_host_memory.c src/intro_host_rtl_text_globals.c
    tests/launcher_intro_rtl_text_host_tests.c)
  target_include_directories(launcher-intro-rtl-text-host-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-rtl-text-host COMMAND launcher-intro-rtl-text-host-tests)
  add_executable(launcher-intro-native-glyph-tests
    decomp/launcher/gate_rtl.c decomp/launcher/gate_exe_helpers.c
    src/intro_host_memory.c src/intro_host_rtl_text_globals.c
    src/intro_host_rtl_pixel.c tests/launcher_intro_native_glyph_tests.c)
  target_include_directories(launcher-intro-native-glyph-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-native-glyph COMMAND launcher-intro-native-glyph-tests)
  add_executable(launcher-intro-native-draw-tests
    decomp/launcher/gate_rtl_draw.c src/intro_host_memory.c
    src/intro_host_rtl_pixel.c tests/launcher_intro_native_draw_tests.c)
  target_include_directories(launcher-intro-native-draw-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-native-draw COMMAND launcher-intro-native-draw-tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin")
    add_test(NAME launcher-intro-original-draw
      COMMAND launcher-intro-native-draw-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin")
    add_test(NAME launcher-intro-original-glyph
      COMMAND launcher-intro-native-glyph-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin)
  endif()
  add_executable(launcher-intro-native-palette-tests
    decomp/launcher/gate_rtl_palette.c
    src/intro_host_memory.c src/intro_host_rtl_palette_globals.c
    src/intro_host_vga_ports.c tests/launcher_intro_native_palette_tests.c)
  target_include_directories(launcher-intro-native-palette-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-native-palette COMMAND launcher-intro-native-palette-tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin")
    add_test(NAME launcher-intro-original-palette
      COMMAND launcher-intro-native-palette-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin)
  endif()
  add_executable(launcher-intro-native-video-mode-tests
    decomp/launcher/gate_rtl_mode.c decomp/launcher/gate_rtl_palette.c
    src/intro_host_memory.c src/intro_host_rtl_pixel.c
    src/intro_host_rtl_palette_globals.c
    src/intro_host_video_mode.c src/intro_host_vga_ports.c
    tests/launcher_intro_native_video_mode_tests.c)
  target_include_directories(launcher-intro-native-video-mode-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-native-video-mode COMMAND launcher-intro-native-video-mode-tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin")
    add_test(NAME launcher-intro-original-video-mode
      COMMAND launcher-intro-native-video-mode-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin)
  endif()
  add_executable(launcher-intro-native-file-integration-tests
    decomp/launcher/gate_rtl_file.c
    decomp/launcher/gate_rtl_file_linkage.c
    decomp/launcher/gate_rtl_resource.c
    decomp/launcher/gate_rtl_loader.c
    decomp/launcher/gate_rtl_image_finish.c
    decomp/launcher/gate_rtl_image_processor.c
    decomp/launcher/gate_vfx_image.c
    decomp/launcher/gate_dos_file_runtime.c
    src/intro_dos_host_files.c src/intro_host_memory.c src/intro_host_vfx.c
    src/intro_host_rtl_globals.c
    tests/launcher_intro_native_file_integration_tests.c
    tests/launcher_intro_planar_oracle.cpp)
  target_include_directories(launcher-intro-native-file-integration-tests PRIVATE decomp/launcher)
  target_link_libraries(launcher-intro-native-file-integration-tests
    PRIVATE gateway_intro_pic_host)
  add_test(NAME launcher-intro-native-file-integration COMMAND launcher-intro-native-file-integration-tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos/GATE_300.PIC")
    add_test(NAME launcher-intro-native-original-pic
      COMMAND launcher-intro-native-file-integration-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-1000.bin" AND
     EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos/GATE_201.PIC")
    add_test(NAME launcher-intro-native-original-planar-pic
      COMMAND launcher-intro-native-file-integration-tests --mode2
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-1000.bin
        ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
  endif()
  add_executable(launcher-intro-host-vfx-tests
    decomp/launcher/gate_rtl_image_processor.c
    decomp/launcher/gate_vfx_image.c
    src/intro_host_vfx.c src/intro_host_memory.c src/intro_host_rtl_globals.c
    tests/launcher_intro_host_vfx_tests.c)
  target_include_directories(launcher-intro-host-vfx-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-intro-host-vfx COMMAND launcher-intro-host-vfx-tests)
  add_executable(launcher-rtl-image-finish-decompile-tests
    decomp/launcher/gate_rtl_image_finish.c
    tests/launcher_rtl_image_finish_decompile_tests.c)
  target_include_directories(launcher-rtl-image-finish-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-image-finish-decompile COMMAND launcher-rtl-image-finish-decompile-tests)
  add_executable(launcher-rtl-image-prepare-decompile-tests
    decomp/launcher/gate_rtl_image_prepare.c
    tests/launcher_rtl_image_prepare_decompile_tests.c)
  target_include_directories(launcher-rtl-image-prepare-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-image-prepare-decompile COMMAND launcher-rtl-image-prepare-decompile-tests)
  add_executable(launcher-rtl-image-processor-decompile-tests
    decomp/launcher/gate_rtl_image_processor.c
    tests/launcher_rtl_image_processor_decompile_tests.c)
  target_include_directories(launcher-rtl-image-processor-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-image-processor-decompile COMMAND launcher-rtl-image-processor-decompile-tests)
  add_executable(launcher-rtl-intro-animation-decompile-tests
    decomp/launcher/gate_rtl_intro_animation.c
    tests/launcher_rtl_intro_animation_decompile_tests.c)
  target_include_directories(launcher-rtl-intro-animation-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-intro-animation-decompile COMMAND launcher-rtl-intro-animation-decompile-tests)
  add_executable(launcher-rtl-intro-sequence-decompile-tests
    decomp/launcher/gate_rtl_intro_sequence.c
    tests/launcher_rtl_intro_sequence_decompile_tests.c)
  target_include_directories(launcher-rtl-intro-sequence-decompile-tests PRIVATE decomp/launcher)
  add_test(NAME launcher-rtl-intro-sequence-decompile COMMAND launcher-rtl-intro-sequence-decompile-tests)
  add_executable(launcher-intro-native-random-tests
    decomp/launcher/gate_c_runtime_random.c
    decomp/launcher/gate_rtl_image_processor.c
    src/intro_host_memory.c tests/launcher_intro_native_random_tests.c)
  target_include_directories(launcher-intro-native-random-tests PRIVATE decomp/launcher)
  target_compile_options(launcher-intro-native-random-tests PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(launcher-intro-native-random-tests PRIVATE -Wl,--gc-sections)
  add_test(NAME launcher-intro-native-random COMMAND launcher-intro-native-random-tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin")
    add_test(NAME launcher-intro-original-random
      COMMAND launcher-intro-native-random-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/build/intro-analysis/at-22000.bin)
  endif()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME launcher-intro-runtime-string-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_runtime_string_binary_tests.py)
    add_test(NAME launcher-intro-runtime-clock-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_runtime_clock_binary_tests.py)
    add_test(NAME launcher-intro-clock-sampler-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_clock_sampler_binary_tests.py)
    add_test(NAME launcher-intro-entry-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_entry_binary_tests.py)
    add_test(NAME launcher-intro-source-c-picture COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_source_c_picture_tests.py
      $<TARGET_FILE:gateway-intro-source-c-picture-dump>)
    add_test(NAME launcher-intro-source-c-full COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_source_c_full_tests.py
      $<TARGET_FILE:gateway-intro-source-c-composite>)
    add_test(NAME launcher-intro-handoff COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_handoff_tests.py)
    add_test(NAME launcher-intro-audio-startup-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_audio_startup_binary_tests.py)
    add_test(NAME launcher-intro-audio-boundary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_audio_boundary_tests.py)
    add_test(NAME launcher-intro-keyboard-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_keyboard_binary_tests.py)
    add_test(NAME launcher-rtl-graphics-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_rtl_graphics_binary_tests.py)
    add_test(NAME launcher-intro-vfx-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_intro_vfx_binary_tests.py)
    add_test(NAME launcher-rtl-file-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_rtl_file_binary_tests.py)
    add_test(NAME launcher-dos-file-runtime-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_dos_file_runtime_binary_tests.py)
    add_test(NAME intro-ghidra-preparation COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/intro_ghidra_tests.py)
  endif()
endif()

# An independent, dependency-light flake output for inspecting, compiling,
# and testing the actual reconstructed C. No emulation or SDL linkage is used.
if(GATEWAY_INTRO_DECOMPILE_ONLY)
  return()
endif()

add_library(nuked_opl3 STATIC third_party/nuked-opl3/opl3.c)
target_include_directories(nuked_opl3 PUBLIC third_party/nuked-opl3)
add_library(gateway_core src/mz.cpp src/rtlink.cpp src/object_flags.cpp src/story_model.cpp src/dos_clock.cpp src/dos_files.cpp src/dos_fcb.cpp src/dos_memory.cpp src/vga.cpp src/opl.cpp src/adlib_driver.cpp src/intro_mdi_player.cpp src/pit.cpp src/pic.cpp src/scene_compositor.cpp src/scene_animation.cpp src/host_scene_preview.cpp src/crusher.cpp src/png.cpp src/regions.cpp src/debug_scene.cpp src/debug_state.cpp src/debug_ui.cpp src/picker_scene_trace.cpp src/picker_capture.cpp src/ui_layout.cpp src/story/intro_presentation.cpp)
target_include_directories(gateway_core PUBLIC src)
target_link_libraries(gateway_core PUBLIC nuked_opl3)
target_compile_options(gateway_core PRIVATE -Wall -Wextra -Wpedantic)
add_executable(gateway-inspect src/inspect.cpp)
target_link_libraries(gateway-inspect PRIVATE gateway_core)
add_executable(gateway-overlay src/overlay.cpp)
target_link_libraries(gateway-overlay PRIVATE gateway_core)
add_executable(gateway-extract src/extract.cpp)
target_link_libraries(gateway-extract PRIVATE gateway_core)
add_executable(gateway-scene src/scene_compose.cpp)
target_link_libraries(gateway-scene PRIVATE gateway_core)
add_executable(gateway-intro-dump src/intro_dump.cpp)
target_link_libraries(gateway-intro-dump PRIVATE gateway_core)
install(TARGETS gateway-intro-dump RUNTIME DESTINATION bin)
add_executable(gateway-story-check tools/check_action_matrix.cpp)
target_link_libraries(gateway-story-check PRIVATE gateway_core)
if(GATEWAY_WITH_SDL)
  find_package(SDL3 CONFIG REQUIRED)
  find_package(Threads REQUIRED)
  add_executable(gateway-intro-source-c-live
    ${GATEWAY_INTRO_SOURCE_C_COMPOSITE_SOURCES}
    src/intro_source_c_live.cpp)
  target_include_directories(gateway-intro-source-c-live PRIVATE decomp/launcher src)
  target_compile_definitions(gateway-intro-source-c-live PRIVATE
    GATE_SOURCE_C_LIBRARY)
  target_compile_options(gateway-intro-source-c-live PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(gateway-intro-source-c-live PRIVATE -Wl,--gc-sections)
  target_link_libraries(gateway-intro-source-c-live PRIVATE
    gateway_intro_pic_host gateway_core SDL3::SDL3 Threads::Threads)
  install(TARGETS gateway-intro-source-c-live RUNTIME DESTINATION bin)
  add_executable(gateway-intro-source-c-replay src/intro_source_c_replay.cpp)
  target_link_libraries(gateway-intro-source-c-replay PRIVATE SDL3::SDL3)
  install(TARGETS gateway-intro-source-c-replay RUNTIME DESTINATION bin)
  add_executable(gateway-intro-source-c-captions
    ${GATEWAY_INTRO_SOURCE_C_CAPTION_SOURCES}
    src/intro_source_c_player.cpp)
  target_include_directories(gateway-intro-source-c-captions PRIVATE
    decomp/launcher src)
  target_compile_options(gateway-intro-source-c-captions PRIVATE
    -ffunction-sections -fdata-sections)
  target_link_options(gateway-intro-source-c-captions PRIVATE -Wl,--gc-sections)
  target_link_libraries(gateway-intro-source-c-captions PRIVATE SDL3::SDL3)
  install(TARGETS gateway-intro-source-c-captions RUNTIME DESTINATION bin)
  add_executable(gateway-intro src/intro_player.cpp)
  target_link_libraries(gateway-intro PRIVATE gateway_core SDL3::SDL3)
  add_executable(gateway-host src/host.cpp)
  target_link_libraries(gateway-host PRIVATE SDL3::SDL3)
  if(BUILD_TESTING)
    add_executable(sdl-input-tests tests/sdl_input_tests.cpp src/sdl_host.cpp)
    target_link_libraries(sdl-input-tests PRIVATE gateway_core SDL3::SDL3)
    add_test(NAME sdl-input COMMAND sdl-input-tests)
    add_test(NAME sdl-quit COMMAND sdl-input-tests --quit-only)
    set_tests_properties(sdl-input sdl-quit PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;SDL_NO_SIGNAL_HANDLERS=0" TIMEOUT 10)
    add_test(NAME sdl-host COMMAND gateway-host --smoke-test)
    set_tests_properties(sdl-host PROPERTIES
      ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 10)
  endif()
  install(TARGETS gateway-host RUNTIME DESTINATION bin)
  install(TARGETS gateway-intro RUNTIME DESTINATION bin)
endif()
if(BUILD_TESTING)
  add_executable(intro-live-frame-mailbox-tests
    tests/intro_live_frame_mailbox_tests.cpp)
  target_include_directories(intro-live-frame-mailbox-tests PRIVATE src)
  add_test(NAME intro-live-frame-mailbox COMMAND intro-live-frame-mailbox-tests)
  add_executable(gatemain-flags-decompile-tests
    decomp/engine/gatemain_flags.c tests/gatemain_flags_decompile_tests.c)
  target_include_directories(gatemain-flags-decompile-tests PRIVATE decomp/engine)
  add_test(NAME gatemain-flags-decompile COMMAND gatemain-flags-decompile-tests)
  add_executable(intro-presentation-tests tests/intro_presentation_tests.cpp)
  target_link_libraries(intro-presentation-tests PRIVATE gateway_core)
  add_test(NAME intro-presentation COMMAND intro-presentation-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
  set_tests_properties(intro-presentation PROPERTIES SKIP_RETURN_CODE 77)
  add_executable(ui-layout-tests tests/ui_layout_tests.cpp)
  target_link_libraries(ui-layout-tests PRIVATE gateway_core)
  add_test(NAME ui-layout COMMAND ui-layout-tests)
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
  add_executable(intro-mdi-player-tests tests/intro_mdi_player_tests.cpp)
  target_link_libraries(intro-mdi-player-tests PRIVATE gateway_core)
  add_test(NAME intro-mdi-player COMMAND intro-mdi-player-tests)
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
  add_executable(scene-compositor-tests tests/scene_compositor_tests.cpp)
  target_link_libraries(scene-compositor-tests PRIVATE gateway_core)
  add_test(NAME scene-compositor COMMAND scene-compositor-tests)
  add_executable(host-scene-preview-tests tests/host_scene_preview_tests.cpp)
  target_link_libraries(host-scene-preview-tests PRIVATE gateway_core)
  add_test(NAME host-scene-preview COMMAND host-scene-preview-tests)
  add_executable(regions-tests tests/regions_tests.cpp)
  target_link_libraries(regions-tests PRIVATE gateway_core)
  add_test(NAME regions COMMAND regions-tests)
  add_executable(debug-scene-tests tests/debug_scene_tests.cpp)
  target_link_libraries(debug-scene-tests PRIVATE gateway_core)
  add_test(NAME debug-scene COMMAND debug-scene-tests)
  add_executable(gateway-original-regions-tests tests/gateway_original_regions_tests.cpp)
  target_link_libraries(gateway-original-regions-tests PRIVATE gateway_core)
  add_test(NAME gateway-original-regions COMMAND gateway-original-regions-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos)
  set_tests_properties(gateway-original-regions PROPERTIES SKIP_RETURN_CODE 77)
  add_executable(debug-state-tests tests/debug_state_tests.cpp)
  target_link_libraries(debug-state-tests PRIVATE gateway_core)
  add_test(NAME debug-state COMMAND debug-state-tests)
  add_executable(debug-ui-tests tests/debug_ui_tests.cpp)
  target_link_libraries(debug-ui-tests PRIVATE gateway_core)
  add_test(NAME debug-ui COMMAND debug-ui-tests)
  add_executable(picker-follow-tests tests/picker_follow_tests.cpp)
  target_link_libraries(picker-follow-tests PRIVATE gateway_core)
  add_test(NAME picker-follow COMMAND picker-follow-tests)
  add_executable(picker-scene-trace-tests tests/picker_scene_trace_tests.cpp)
  target_link_libraries(picker-scene-trace-tests PRIVATE gateway_core)
  add_test(NAME picker-scene-trace COMMAND picker-scene-trace-tests)
  add_executable(picker-capture-tests tests/picker_capture_tests.cpp)
  target_link_libraries(picker-capture-tests PRIVATE gateway_core)
  add_test(NAME picker-capture COMMAND picker-capture-tests)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME picker-trace-check COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/picker_trace_tests.py)
    add_test(NAME picker-trace-report COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/report_picker_trace_tests.py)
    add_test(NAME picker-follow-compare COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_picker_follow_tests.py)
    add_test(NAME picker-follow-report COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/report_picker_follow_tests.py)
    add_test(NAME picker-scene-report COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/report_picker_scene_tests.py)
    add_test(NAME picker-scene-compare COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_picker_scene_tests.py)
    add_test(NAME picker-capture-compare COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/compare_picker_captures_tests.py)
    add_test(NAME picker-click-trials COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/picker_click_trials_tests.py)
    add_test(NAME picker-trial-state-match COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/match_picker_trial_states_tests.py)
    add_test(NAME picker-trial-target-rank COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/rank_picker_trial_targets_tests.py)
    add_test(NAME local-static-unicorn-builder COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_probe_from_unicorn_zip_tests.py)
    add_test(NAME gateway-original-assets COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/gateway_original_assets_tests.py
      --data ${CMAKE_CURRENT_SOURCE_DIR}/../gateway-dos
      --extract $<TARGET_FILE:gateway-extract>
      --overlay $<TARGET_FILE:gateway-overlay>)
    set_tests_properties(gateway-original-assets PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    add_test(NAME pic-extract COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/extract_pic_tests.py $<TARGET_FILE:gateway-extract>)
    add_test(NAME story-disassembly COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/disassemble_story_tests.py)
    add_test(NAME x86-16-decoder COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/x86_16_tests.py)
    add_test(NAME callgraph-classification COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/report_callgraph_tests.py)
    add_test(NAME rtlink-resident COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/rtlink_resident_tests.py)
    add_test(NAME story-admission-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/summarize_admission_trace_tests.py)
    add_test(NAME story-visibility-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/visibility_trace_tests.py)
    add_test(NAME story-rgn-memory-trace COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/rgn_memory_trace_tests.py)
    add_test(NAME story-region-conditions COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/region_condition_tests.py)
    add_test(NAME story-picture-conditions COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/picture_condition_tests.py)
    add_test(NAME story-rtl-animation COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/rtl_animation_tests.py)
    add_test(NAME intro-program-inspector COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/intro_program_tests.py)
    add_test(NAME launcher-decompile-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_decompile_binary_tests.py)
    add_test(NAME launcher-rtl-decompile-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_rtl_decompile_binary_tests.py)
    add_test(NAME launcher-rtl-font-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher_rtl_font_binary_tests.py)
    add_test(NAME gatemain-flags-decompile-binary COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/gatemain_flags_decompile_binary_tests.py)
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
    add_test(NAME rank-engine-services COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/rank_engine_services_tests.py)
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
    add_test(NAME golden-replay-plan COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden_tests.py)
    add_test(NAME gateway-save-inspect COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/inspect_gateway_save_tests.py)
    add_test(NAME story-action-batch COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_action_matrix_tests.py)
  endif()
endif()
install(TARGETS gateway-inspect gateway-extract gateway-scene gateway-overlay gateway-story-check RUNTIME DESTINATION bin)

if(GATEWAY_WITH_UNICORN)
  find_path(UNICORN_INCLUDE_DIR unicorn/unicorn.h REQUIRED)
  find_library(UNICORN_LIBRARY unicorn REQUIRED)
  add_executable(gateway-probe src/probe.cpp src/lift_runtime.cpp)
  target_include_directories(gateway-probe PRIVATE ${UNICORN_INCLUDE_DIR})
  target_link_libraries(gateway-probe PRIVATE gateway_core ${UNICORN_LIBRARY} ${CMAKE_DL_LIBS})
  if(BUILD_TESTING)
    add_executable(native-picker-oracle-tests tests/native_picker_oracle_tests.cpp)
    target_include_directories(native-picker-oracle-tests PRIVATE ${UNICORN_INCLUDE_DIR})
    target_link_libraries(native-picker-oracle-tests PRIVATE gateway_core ${UNICORN_LIBRARY})
    add_test(NAME native-picker-oracle COMMAND native-picker-oracle-tests)
    set_tests_properties(native-picker-oracle PROPERTIES SKIP_RETURN_CODE 77)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME rtlink-tool COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/overlay_tool_tests.py $<TARGET_FILE:gateway-overlay>)
    add_test(NAME real-mode-probe COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/probe_tests.py $<TARGET_FILE:gateway-probe>)
    add_executable(lift-check tests/lift_check.cpp)
    target_include_directories(lift-check PRIVATE ${UNICORN_INCLUDE_DIR})
    target_link_libraries(lift-check PRIVATE ${UNICORN_LIBRARY} ${CMAKE_DL_LIBS})
    add_test(NAME lift-semantics COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/lift_semantics_tests.py $<TARGET_FILE:lift-check>)
    set_tests_properties(lift-semantics PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
  endif()
  if(GATEWAY_WITH_SDL)
    add_executable(gateway src/probe.cpp src/sdl_host.cpp src/lift_runtime.cpp)
    target_compile_definitions(gateway PRIVATE GATEWAY_SDL=1)
    target_include_directories(gateway PRIVATE ${UNICORN_INCLUDE_DIR})
    target_link_libraries(gateway PRIVATE gateway_core ${UNICORN_LIBRARY} SDL3::SDL3
      ${CMAKE_DL_LIBS})
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
