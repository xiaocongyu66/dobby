#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${ROOT_DIR}/build/linux-x86_64"
OUT_DIR="${ROOT_DIR}/prebuilt/linux-x86_64"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DDOBBY_BUILD_EXAMPLE=ON \
  -DDOBBY_BUILD_TEST=OFF
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
mkdir -p "${OUT_DIR}"
cp -f "${BUILD_DIR}/libdobby.so" "${OUT_DIR}/"
cp -f "${BUILD_DIR}/libdobby.a" "${OUT_DIR}/"
cp -f "${BUILD_DIR}/examples/socket_example" "${OUT_DIR}/" 2>/dev/null || true
cp -f "${ROOT_DIR}/include/dobby.h" "${OUT_DIR}/dobby.h"
echo "Built Linux -> ${OUT_DIR}"
