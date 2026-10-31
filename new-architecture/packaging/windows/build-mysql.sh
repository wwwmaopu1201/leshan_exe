#!/bin/bash
set -euo pipefail
qt=/opt/qt/6.8.2/mingw_64
work=/build/mysql-driver
mkdir -p "$work"
if [ ! -f "$qt/plugins/sqldrivers/qsqlmysql.dll" ]; then
  if [ ! -f "$work/mariadb-src/CMakeLists.txt" ]; then
  curl -L --fail --retry 3 --connect-timeout 20 --max-time 180 \
    https://archive.mariadb.org/connector-c-3.4.5/mariadb-connector-c-3.4.5-src.tar.gz \
    -o "$work/mariadb.tar.gz"
  mkdir -p "$work/mariadb-src"
  tar -xf "$work/mariadb.tar.gz" -C "$work/mariadb-src" --strip-components=1
  fi
  cmake -S "$work/mariadb-src" -B "$work/mariadb-build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/src/new-architecture/packaging/windows/mingw-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/mariadb \
    -DWITH_UNIT_TESTS=OFF -DWITH_SSL=SCHANNEL -DWITH_CURL=OFF \
    -DWITH_EXTERNAL_ZLIB=OFF -DWITH_MSI=OFF -DWITH_SIGNCODE=OFF
  cmake --build "$work/mariadb-build" --parallel 6
  cmake --install "$work/mariadb-build"
  if [ ! -f /opt/qt/6.8.2/Src/qtbase/CMakeLists.txt ]; then
    /opt/aqt/bin/aqt install-src windows desktop 6.8.2 --archives qtbase -O /opt/qt
  fi
  cmake -S /opt/qt/6.8.2/Src/qtbase/src/plugins/sqldrivers -B "$work/qt-build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=/src/new-architecture/packaging/windows/mingw-toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$qt" -DQT_HOST_PATH=/usr \
    -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/aarch64-linux-gnu/cmake \
    -DMySQL_INCLUDE_DIR=/opt/mariadb/include/mariadb \
    -DMySQL_LIBRARY=/opt/mariadb/lib/mariadb/liblibmariadb.dll.a \
    -DCMAKE_INSTALL_PREFIX="$qt"
  cmake --build "$work/qt-build" --target QMYSQLDriverPlugin --parallel 6
  find "$work/qt-build" -name 'qsqlmysql.dll' -exec cp '{}' "$qt/plugins/sqldrivers/" \;
  find /opt/mariadb -name 'libmariadb.dll' -exec cp '{}' "$qt/bin/" \;
fi
