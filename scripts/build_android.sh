#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [[ -z "${NDK_HOME}" ]]; then
  echo "ANDROID_NDK_HOME or ANDROID_NDK_ROOT is required" >&2
  exit 1
fi
TOOLCHAIN="${NDK_HOME}/build/cmake/android.toolchain.cmake"
if [[ ! -f "${TOOLCHAIN}" ]]; then
  echo "Android toolchain not found: ${TOOLCHAIN}" >&2
  exit 1
fi

ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-21}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

# arm64-v8a is the default release ABI. The legacy ARMv7 sources in upstream
# Dobby are incomplete in this fork, so armeabi-v7a is not built by default.
# You can still pass ABIs explicitly, for example: ./scripts/build_android.sh arm64-v8a x86_64
if [[ $# -gt 0 ]]; then
  ABIS=("$@")
else
  read -r -a ABIS <<< "${DOBBY_ANDROID_ABIS:-arm64-v8a}"
fi

for ABI in "${ABIS[@]}"; do
  BUILD_DIR="${ROOT_DIR}/build/android/${ABI}"
  OUT_DIR="${ROOT_DIR}/prebuilt/android/${ABI}"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" \
    -DANDROID_ABI="${ABI}" \
    -DANDROID_PLATFORM="${ANDROID_PLATFORM}" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DDOBBY_ANDROID_USE_XDL=ON \
    -DDOBBY_BUILD_EXAMPLE=OFF \
    -DDOBBY_BUILD_TEST=OFF
  cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
  mkdir -p "${OUT_DIR}"
  cp -f "${BUILD_DIR}/libdobby.so" "${OUT_DIR}/" 2>/dev/null || true
  cp -f "${BUILD_DIR}/libdobby.a" "${OUT_DIR}/" 2>/dev/null || true
  cp -f "${ROOT_DIR}/include/dobby.h" "${OUT_DIR}/dobby.h"
  mkdir -p "${ROOT_DIR}/prebuilt/android/include"
  cp -f "${ROOT_DIR}/include/dobby.h" "${ROOT_DIR}/prebuilt/android/include/dobby.h"
  echo "Built ${ABI} -> ${OUT_DIR}"
done
