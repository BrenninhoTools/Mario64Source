#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${VERSION:-us}"
SDL_TAG="${SDL_TAG:-release-2.30.9}"
API="${ANDROID_API:-21}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    echo "ANDROID_NDK_HOME is not set" >&2
    exit 1
fi

case "$(uname -s)" in
    Linux) HOST_TAG=linux-x86_64 ;;
    Darwin) HOST_TAG=darwin-x86_64 ;;
    MINGW*|MSYS*|CYGWIN*) HOST_TAG=windows-x86_64 ;;
    *) echo "Unsupported host" >&2; exit 1 ;;
esac

if [ ! -f "$ROOT/baserom.$VERSION.z64" ]; then
    echo "Missing baserom.$VERSION.z64 in $ROOT" >&2
    exit 1
fi

SDL_DIR="$ROOT/android/app/jni/SDL"
if [ ! -d "$SDL_DIR" ]; then
    git clone --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL "$SDL_DIR"
fi

SDL_BUILD="$ROOT/android/build/sdl2-cmake"
SDL_OUT="$ROOT/android/build/sdl2/arm64-v8a"
cmake -S "$SDL_DIR" -B "$SDL_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM="android-$API" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=ON \
    -DSDL_STATIC=OFF \
    -DSDL_TEST=OFF
cmake --build "$SDL_BUILD" -j "$JOBS"
mkdir -p "$SDL_OUT"
cp "$SDL_BUILD/libSDL2.so" "$SDL_OUT/libSDL2.so"

make -C "$ROOT" -f Makefile.port \
    PLATFORM=android \
    VERSION="$VERSION" \
    ANDROID_NDK_HOME="$ANDROID_NDK_HOME" \
    NDK_HOST_TAG="$HOST_TAG" \
    ANDROID_API="$API" \
    SDL2_INCLUDE="$SDL_DIR/include" \
    SDL2_LIB="$SDL_OUT" \
    -j "$JOBS"

JNI_DIR="$ROOT/android/app/src/main/jniLibs/arm64-v8a"
mkdir -p "$JNI_DIR"
cp "$SDL_OUT/libSDL2.so" "$JNI_DIR/libSDL2.so"
cp "$ROOT/build/${VERSION}_android/libmain.so" "$JNI_DIR/libmain.so"
echo "Native libraries ready in $JNI_DIR"
