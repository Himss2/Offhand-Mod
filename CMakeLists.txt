cmake_minimum_required(VERSION 3.22)

project(
    levi_offhand
    LANGUAGES CXX
)


set(
    CMAKE_CXX_STANDARD
    20
)

set(
    CMAKE_CXX_STANDARD_REQUIRED
    ON
)

set(
    CMAKE_CXX_EXTENSIONS
    OFF
)


if(NOT ANDROID)

    message(
        FATAL_ERROR
        "LeviOffhand is an Android native mod."
    )

endif()


include(FetchContent)


# ============================================================
# Pin preloader ABI.
# ============================================================

FetchContent_Declare(

    preloader_android

    GIT_REPOSITORY
        https://github.com/LiteLDev/preloader-android.git

    GIT_TAG
        47edce4b3f2101bbb99b39b05b129365e8e7a4f1
)


FetchContent_MakeAvailable(
    preloader_android
)


if(ANDROID_ABI)

    set(
        LEVI_OFFHAND_OUTPUT_ABI
        "${ANDROID_ABI}"
    )

else()

    set(
        LEVI_OFFHAND_OUTPUT_ABI
        "android"
    )

endif()


set(
    CMAKE_LIBRARY_OUTPUT_DIRECTORY

    "${CMAKE_BINARY_DIR}/out/${LEVI_OFFHAND_OUTPUT_ABI}"
)


# ============================================================
# Levi Offhand
# ============================================================

add_library(

    levi_offhand

    SHARED

    src/LeviOffhandMod.cpp

    src/runtime/NativeOffhandPolicy.cpp

    src/runtime/AutoInsertRouting.cpp

    src/runtime/ActionHandContext.cpp

    src/runtime/NativeCapabilityProbe.cpp

    src/runtime/HandActionRouter.cpp

    src/runtime/NativeSemanticBridge.cpp

    src/render/OffhandBlockRenderPatch.cpp
)


set_target_properties(

    levi_offhand

    PROPERTIES

    OUTPUT_NAME
        "levi_offhand"
)


target_include_directories(

    levi_offhand

    PRIVATE

    "${CMAKE_CURRENT_LIST_DIR}/src"
)


target_link_libraries(

    levi_offhand

    PRIVATE

    preloader
)


# ============================================================
# Android logging
# ============================================================

find_library(
    LEVI_OFFHAND_LOG_LIB
    log
)


if(LEVI_OFFHAND_LOG_LIB)

    target_link_libraries(

        levi_offhand

        PRIVATE

        "${LEVI_OFFHAND_LOG_LIB}"
    )

endif()


# ============================================================
# dladdr / dynamic loader
# ============================================================

find_library(
    LEVI_OFFHAND_DL_LIB
    dl
)


if(LEVI_OFFHAND_DL_LIB)

    target_link_libraries(

        levi_offhand

        PRIVATE

        "${LEVI_OFFHAND_DL_LIB}"
    )

endif()


# ============================================================
# Compiler options
# ============================================================

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


if(
    CMAKE_BUILD_TYPE
    STREQUAL
    "Release"
)

    target_compile_options(

        levi_offhand

        PRIVATE

        -Oz
    )

endif()


# ============================================================
# Android 16 KiB page-size compatibility
# ============================================================

target_link_options(

    levi_offhand

    PRIVATE

    -Wl,--gc-sections

    -Wl,-z,max-page-size=16384
)
