#!/usr/bin/env bash

set -euo pipefail


ROOT="$(
    cd "$(dirname "${BASH_SOURCE[0]}")/.."
    pwd
)"


ABI="${ABI:-arm64-v8a}"

BUILD_TYPE="${BUILD_TYPE:-Release}"

NDK_VERSION="${LEVI_NDK_VERSION:-28.2.13676358}"


find_ndk() {

    local candidate=""


    if [[ -n "${ANDROID_HOME:-}" ]]; then

        candidate="$ANDROID_HOME/ndk/$NDK_VERSION"


        if [[ -f \
            "$candidate/build/cmake/android.toolchain.cmake" \
        ]]; then

            printf '%s\n' "$candidate"

            return 0
        fi
    fi


    if [[ -n "${ANDROID_SDK_ROOT:-}" ]]; then

        candidate="$ANDROID_SDK_ROOT/ndk/$NDK_VERSION"


        if [[ -f \
            "$candidate/build/cmake/android.toolchain.cmake" \
        ]]; then

            printf '%s\n' "$candidate"

            return 0
        fi
    fi


    if [[ -n "${ANDROID_NDK_HOME:-}" ]] &&
       [[ -f \
           "$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
       ]]; then

        printf '%s\n' "$ANDROID_NDK_HOME"

        return 0
    fi


    if [[ -n "${ANDROID_NDK_ROOT:-}" ]] &&
       [[ -f \
           "$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
       ]]; then

        printf '%s\n' "$ANDROID_NDK_ROOT"

        return 0
    fi


    return 1
}


if ! NDK="$(find_ndk)"; then

    echo \
        "Android NDK $NDK_VERSION not found." \
        >&2

    exit 1
fi


TOOLCHAIN="$NDK/build/cmake/android.toolchain.cmake"


BUILD_DIR="$ROOT/build/android-$ABI-$BUILD_TYPE"

DIST_DIR="$ROOT/dist/$ABI"

PACKAGE_DIR="$DIST_DIR/levi-offhand"

LEVIPACK="$DIST_DIR/levi-offhand-v0.2.61.levipack"


echo "Using Android NDK: $NDK"

echo "ABI: $ABI"

echo "Build type: $BUILD_TYPE"


cmake \
    -S "$ROOT" \
    -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM=android-24 \
    -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"


cmake \
    --build "$BUILD_DIR" \
    --target levi_offhand


rm -rf "$DIST_DIR"


mkdir -p "$PACKAGE_DIR"


cp \
    "$ROOT/manifest.json" \
    "$PACKAGE_DIR/manifest.json"


cp \
    "$BUILD_DIR/out/$ABI/liblevi_offhand.so" \
    "$PACKAGE_DIR/liblevi_offhand.so"


(
    cd "$PACKAGE_DIR"

    zip \
        -qr \
        "$LEVIPACK" \
        .
)


echo "Built: $LEVIPACK"
