cmake_minimum_required(VERSION 3.22)

project(
    levi_offhand
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT ANDROID)
    message(FATAL_ERROR "LeviOffhand is an Android native mod.")
endif()

include(FetchContent)

FetchContent_Declare(
    preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG 47edce4b3f2101bbb99b39b05b129365e8e7a4f1
)
FetchContent_MakeAvailable(preloader_android)

if(ANDROID_ABI)
    set(LEVI_OFFHAND_OUTPUT_ABI "${ANDROID_ABI}")
else()
    set(LEVI_OFFHAND_OUTPUT_ABI "android")
endif()

set(
    CMAKE_LIBRARY_OUTPUT_DIRECTORY
    "${CMAKE_BINARY_DIR}/out/${LEVI_OFFHAND_OUTPUT_ABI}"
)

# Keep the accepted renderer implementation in src/render frozen.  Minecraft
# 1.26.51.1 compatibility is generated into the build tree by translating only
# binary targets/signatures; the visual behavior/calibration source stays byte
# identical to the pre-update renderer.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(LEVI_OFFHAND_GENERATED_ROOT "${CMAKE_BINARY_DIR}/generated")
set(
    LEVI_OFFHAND_RENDER_SOURCE
    "${CMAKE_BINARY_DIR}/generated/render/OffhandBlockRenderPatch.cpp"
)
execute_process(
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/scripts/generate_render_126511_compat.py"
        --source-root "${CMAKE_CURRENT_LIST_DIR}"
        --output-root "${LEVI_OFFHAND_GENERATED_ROOT}"
    RESULT_VARIABLE LEVI_OFFHAND_RENDER_GENERATOR_RESULT
    OUTPUT_VARIABLE LEVI_OFFHAND_RENDER_GENERATOR_OUTPUT
    ERROR_VARIABLE LEVI_OFFHAND_RENDER_GENERATOR_ERROR
)
if(NOT LEVI_OFFHAND_RENDER_GENERATOR_RESULT EQUAL 0)
    message(
        FATAL_ERROR
        "Minecraft 1.26.51.1 renderer compatibility generation failed:\n"
        "${LEVI_OFFHAND_RENDER_GENERATOR_OUTPUT}"
        "${LEVI_OFFHAND_RENDER_GENERATOR_ERROR}"
    )
endif()
message(STATUS "${LEVI_OFFHAND_RENDER_GENERATOR_OUTPUT}")

add_library(
    levi_offhand
    SHARED
    src/LeviOffhandMod.cpp
    src/ui/SwapButton.cpp
    src/runtime/OffhandSwapRuntime.cpp
    src/runtime/NativeOffhandPolicy.cpp
    src/runtime/AutoInsertRouting.cpp
    src/runtime/ActionHandContext.cpp
    src/runtime/NativeCapabilityProbe.cpp
    src/runtime/HandActionRouter.cpp
    src/runtime/NativeSemanticBridge.cpp
    src/runtime/RightUseRouter.cpp
    "${LEVI_OFFHAND_RENDER_SOURCE}"
)

set_target_properties(
    levi_offhand
    PROPERTIES
    OUTPUT_NAME "levi_offhand"
)

target_include_directories(
    levi_offhand
    PRIVATE
    # Generated renderer headers must win over src/render while building the
    # 1.26.51.1 compatibility copy.
    "${LEVI_OFFHAND_GENERATED_ROOT}"
    "${CMAKE_CURRENT_LIST_DIR}/src"
)

target_link_libraries(levi_offhand PRIVATE preloader)

find_library(LEVI_OFFHAND_LOG_LIB log)
if(LEVI_OFFHAND_LOG_LIB)
    target_link_libraries(levi_offhand PRIVATE "${LEVI_OFFHAND_LOG_LIB}")
endif()

find_library(LEVI_OFFHAND_DL_LIB dl)
if(LEVI_OFFHAND_DL_LIB)
    target_link_libraries(levi_offhand PRIVATE "${LEVI_OFFHAND_DL_LIB}")
endif()

target_compile_options(
    levi_offhand
    PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -fvisibility=hidden
    -fvisibility-inlines-hidden
    -ffunction-sections
    -fdata-sections
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(
        levi_offhand
        PRIVATE
        -Oz
        -fno-rtti
    )
endif()

target_link_options(
    levi_offhand
    PRIVATE
    -Wl,--gc-sections
    -Wl,-z,max-page-size=16384
)
