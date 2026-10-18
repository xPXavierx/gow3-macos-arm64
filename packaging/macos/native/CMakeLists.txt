cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Native runtime architecture")
project(gow3_darwin_native LANGUAGES C ASM)
if(NOT APPLE OR NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "This target requires native macOS ARM64")
endif()
set(CMAKE_C_STANDARD 11)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
find_package(zycore CONFIG REQUIRED)
if(NOT TARGET Zycore)
    add_library(Zycore ALIAS Zycore::Zycore)
endif()
find_package(zydis CONFIG REQUIRED)
add_subdirectory(../aot aot)
file(READ "${GOW3_RECOMP_SOURCE_DIRECTORY}/src/cpu/interp.c" CPU_INTERPRETER_SOURCE)
if(NOT CPU_INTERPRETER_SOURCE MATCHES "GOW3_AOT_ONLY")
    message(FATAL_ERROR "Apply the bundled bloodborne-recomp ARM64 patch before building: strict AOT enforcement is required")
endif()
file(GLOB CPU_SOURCES "${GOW3_RECOMP_SOURCE_DIRECTORY}/src/cpu/*.c")
add_library(gow3_aot_cpu STATIC ${CPU_SOURCES} "${GOW3_RECOMP_SOURCE_DIRECTORY}/src/cpu/hostcall.S")
target_compile_definitions(gow3_aot_cpu PRIVATE GOW3_AOT_ONLY=1)
target_compile_options(gow3_aot_cpu PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_libraries(gow3_aot_cpu PUBLIC Zydis::Zydis)
find_package(SDL3 CONFIG REQUIRED)
find_path(FFMPEG_INCLUDE_DIR libavcodec/avcodec.h REQUIRED)
foreach(component avcodec avutil swscale)
    find_library(FFMPEG_${component}_LIBRARY NAMES ${component} REQUIRED)
    list(APPEND FFMPEG_LIBRARIES "${FFMPEG_${component}_LIBRARY}")
endforeach()
file(GLOB ATRAC9_SOURCES "${ROOT}/third_party/LibAtrac9/C/src/*.c")
add_library(atrac9 STATIC ${ATRAC9_SOURCES})
file(GLOB RUNTIME_SOURCES CONFIGURE_DEPENDS "${ROOT}/src/runtime*.c")
set(GOW3_GPU_LIBRARY "" CACHE FILEPATH "Compiled ARM64 libgow3gpu.dylib; empty builds a CPU-only loader")
add_library(runtime STATIC ${RUNTIME_SOURCES} "${ROOT}/src/compat_macos.c")
if(GOW3_GPU_LIBRARY)
    execute_process(COMMAND /usr/bin/lipo "${GOW3_GPU_LIBRARY}" -archs
        RESULT_VARIABLE gpu_arch_result OUTPUT_VARIABLE gpu_arch OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT gpu_arch_result EQUAL 0 OR NOT gpu_arch STREQUAL "arm64")
        message(FATAL_ERROR "GOW3_GPU_LIBRARY must be a compiled ARM64-only renderer")
    endif()
    add_library(gow3gpu SHARED IMPORTED)
    set_target_properties(gow3gpu PROPERTIES IMPORTED_LOCATION "${GOW3_GPU_LIBRARY}")
    find_package(Vulkan REQUIRED)
    target_sources(runtime PRIVATE "${ROOT}/src/vulkan_smoke.c")
    target_link_libraries(runtime PUBLIC gow3gpu Vulkan::Vulkan)
else()
    target_sources(runtime PRIVATE ../headless_gpu.c)
endif()
target_compile_definitions(runtime PUBLIC GOW3_ARM64=1)
target_include_directories(runtime PUBLIC "${ROOT}" "${ROOT}/src" "${FFMPEG_INCLUDE_DIR}" "${GOW3_RECOMP_SOURCE_DIRECTORY}/src")
target_compile_options(runtime PRIVATE -Wall -Wextra -Werror -fno-omit-frame-pointer)
target_link_libraries(runtime PUBLIC gow3_aot_cpu atrac9 SDL3::SDL3 ${FFMPEG_LIBRARIES})
set(NATIVE_HOST_SOURCES "${ROOT}/src/probe.c" "${ROOT}/src/isa_rewrite.c" "${ROOT}/src/isa_emulate.c")
add_executable(gow3-probe-arm64 ${NATIVE_HOST_SOURCES})
# Game Mode evaluates the actual game process. Keep the command-line host for
# loader tests, and give the graphical host its own native application identity.
add_executable(gow3-native-game MACOSX_BUNDLE ${NATIVE_HOST_SOURCES})
set_target_properties(gow3-native-game PROPERTIES
    OUTPUT_NAME gow3-probe-arm64
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in")
# Seal the local bundle after linking; the linker's bare Mach-O ad-hoc signature
# does not seal Info.plist/application resources. No developer certificate or
# hardened-runtime entitlement is required for this local development build.
add_custom_command(TARGET gow3-native-game POST_BUILD
    COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:gow3-native-game>"
    VERBATIM)
foreach(host gow3-probe-arm64 gow3-native-game)
    target_compile_options(${host} PRIVATE -Wall -Wextra -Werror -fno-omit-frame-pointer)
    target_link_libraries(${host} PRIVATE runtime)
    # The renderer calls runtime exports through its dynamic library boundary.
    target_link_options(${host} PRIVATE -Wl,-export_dynamic)
endforeach()
enable_testing()
foreach(name runtime sema)
    add_executable(${name}-test "${ROOT}/tests/test_${name}.c")
    target_compile_options(${name}-test PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_libraries(${name}-test PRIVATE runtime)
    add_test(NAME ${name}-test COMMAND ${name}-test)
    set_tests_properties(${name}-test PROPERTIES TIMEOUT 60)
endforeach()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_path(ZYDIS_INCLUDE_DIR Zydis/Zydis.h REQUIRED)
add_test(NAME arm64-loader-integration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/verify_loader.py" "$<TARGET_FILE:gow3-probe-arm64>"
    "$<TARGET_FILE:gow3-aot-recomp>" "${GOW3_RECOMP_SOURCE_DIRECTORY}"
    "${ZYDIS_INCLUDE_DIR}" "${CMAKE_C_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/loader-verification")
set_tests_properties(arm64-loader-integration PROPERTIES TIMEOUT 60)

add_test(NAME arm64-jump-table COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/verify_jump_table.py" "$<TARGET_FILE:gow3-probe-arm64>"
    "$<TARGET_FILE:gow3-aot-recomp>" "${GOW3_RECOMP_SOURCE_DIRECTORY}"
    "${ZYDIS_INCLUDE_DIR}" "${CMAKE_C_COMPILER}" "${CMAKE_CURRENT_BINARY_DIR}/jump-table-verification")
set_tests_properties(arm64-jump-table PROPERTIES TIMEOUT 60)
add_test(NAME arm64-poll-wait COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_LIST_DIR}/verify_poll_wait.py" "$<TARGET_FILE:gow3-aot-recomp>"
    "${GOW3_RECOMP_SOURCE_DIRECTORY}" "${ZYDIS_INCLUDE_DIR}" "${CMAKE_C_COMPILER}"
    "${CMAKE_CURRENT_BINARY_DIR}/poll-verification")
set_tests_properties(arm64-poll-wait PROPERTIES TIMEOUT 60)
if(CMAKE_OSX_SYSROOT)
    # These Python fixtures invoke the compiler directly, outside CMake's driver flags.
    set_tests_properties(arm64-loader-integration arm64-jump-table arm64-poll-wait PROPERTIES
        ENVIRONMENT "SDKROOT=${CMAKE_OSX_SYSROOT}")
endif()
