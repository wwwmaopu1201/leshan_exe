#!/bin/bash
set -euo pipefail
source_root=/src/new-architecture
output=/src/new-architecture/dist-windows-installers
qt=/opt/qt/6.8.2/mingw_64
mkdir -p "$output"
for product in client server; do
  build=/build/$product
  cmake -S "$source_root/$product" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$source_root/packaging/windows/mingw-toolchain.cmake" \
    -DCMAKE_PREFIX_PATH="$qt" -DQT_HOST_PATH=/usr \
    -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/aarch64-linux-gnu/cmake \
    '-DCMAKE_CXX_FLAGS=-DNOMINMAX -Wa,-mbig-obj'
  cmake --build "$build" --target boer-qt-$product --parallel 6
  if [ "$product" = server ]; then bash "$source_root/packaging/windows/build-mysql.sh"; fi
  python3 "$source_root/packaging/windows/package.py" "$product" "$build" "$qt" "$output"
done

python3 "$source_root/packaging/windows/verify.py"

python3 "$source_root/packaging/windows/release.py"
