cmake_minimum_required(VERSION 3.22.1)
project(sf2_android LANGUAGES C CXX)
if(NOT ANDROID OR NOT ANDROID_ABI MATCHES "^(arm64-v8a|x86_64)$")
    message(FATAL_ERROR "Use the Android NDK with arm64-v8a or x86_64")
endif()
get_filename_component(SF2_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(DEPS "${SF2_ROOT}/build/dependencies")
set(ANDROID_DEPS "${DEPS}/android")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_JOB_POOLS compile_pool=6)
set(CMAKE_JOB_POOL_COMPILE compile_pool)

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${DEPS}/SDL3-3.4.16" sdl EXCLUDE_FROM_ALL)

set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_COPYRIGHT_CHECK ON CACHE BOOL "" FORCE)
# shaderc 2025.4 uses a generator expression for glslang's install option;
# keep all export sets enabled together (we never run the install target).
set(SHADERC_SKIP_INSTALL OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_WERROR_COMPILE OFF CACHE BOOL "" FORCE)
set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SHADERC_GLSLANG_DIR "${ANDROID_DEPS}/glslang/source" CACHE STRING "" FORCE)
set(SHADERC_SPIRV_TOOLS_DIR "${ANDROID_DEPS}/spirv-tools/source" CACHE STRING "" FORCE)
set(SHADERC_SPIRV_HEADERS_DIR "${ANDROID_DEPS}/spirv-headers/source" CACHE STRING "" FORCE)
set(SPIRV-Headers_SOURCE_DIR "${ANDROID_DEPS}/spirv-headers/source" CACHE STRING "" FORCE)
add_subdirectory("${ANDROID_DEPS}/shaderc/source" shaderc EXCLUDE_FROM_ALL)

file(GLOB RT_SOURCES CONFIGURE_DEPENDS
    "${SF2_ROOT}/runtime/*.c" "${SF2_ROOT}/runtime/hle/*.c"
    "${SF2_ROOT}/runtime/services/*.c" "${SF2_ROOT}/runtime/ui/*.c"
    "${SF2_ROOT}/runtime/nvn/*.c" "${SF2_ROOT}/runtime/nvn/maxwell/*.c"
    "${SF2_ROOT}/runtime/nvn/vulkan/*.c" "${SF2_ROOT}/runtime/nvn/vulkan/*.cpp")
file(GLOB GEN_SOURCES CONFIGURE_DEPENDS
    "${SF2_ROOT}/game/gen/main/*.c" "${SF2_ROOT}/game/gen/sdk/*.c")
foreach(module main sdk)
    if(NOT EXISTS "${SF2_ROOT}/game/gen/${module}/${module}_table.c")
        message(FATAL_ERROR "Generate the game first: make gen (portable --simd=c)")
    endif()
endforeach()

add_library(game_code OBJECT ${GEN_SOURCES})
target_include_directories(game_code PRIVATE "${SF2_ROOT}/runtime")
target_compile_options(game_code PRIVATE -O1 -g0 -fno-strict-aliasing
    -fvisibility=hidden -Wno-return-type -Wno-unused-label)
# Same slow optimizer cases as the desktop Makefile.
set(SLOW_MAIN 047 183 662 704 735 746 759 048 188 526 673 715 770)
foreach(chunk IN LISTS SLOW_MAIN)
    set_source_files_properties("${SF2_ROOT}/game/gen/main/main_${chunk}.c"
        PROPERTIES COMPILE_OPTIONS -O0)
endforeach()
set_source_files_properties("${SF2_ROOT}/game/gen/sdk/sdk_090.c" PROPERTIES COMPILE_OPTIONS -O0)

add_library(main SHARED ${RT_SOURCES} "${SF2_ROOT}/runtime/platform/android.c"
    $<TARGET_OBJECTS:game_code>)
target_include_directories(main PRIVATE "${SF2_ROOT}/runtime" "${SF2_ROOT}/runtime/nvn"
    "${SF2_ROOT}/third_party/smaa" "${SF2_ROOT}/third_party/smaa/Textures"
    "${ANDROID_DEPS}/vma/source/include")
target_compile_options(main PRIVATE -O2 -fno-strict-aliasing -fno-omit-frame-pointer
    -Wall -Wno-unused-function -Wno-format)
if(ANDROID_ABI STREQUAL "x86_64")
    target_compile_options(game_code PRIVATE -mcx16)
    target_compile_options(main PRIVATE -mcx16)
endif()
target_link_options(main PRIVATE -Wl,-z,max-page-size=16384 -Wl,--no-undefined)
target_link_libraries(main PRIVATE SDL3::SDL3 shaderc vulkan android log atomic m dl)
