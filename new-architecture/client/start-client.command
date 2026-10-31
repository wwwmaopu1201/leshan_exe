#!/bin/zsh
set -euo pipefail

export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/sbin:/usr/bin:/bin:/sbin:$PATH"
unset QT_QPA_PLATFORM

script_dir="${0:A:h}"
build_dir="$script_dir/build"
app_binary="$build_dir/bohr-qt-client.app/Contents/MacOS/bohr-qt-client"

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
  cmake -S "$script_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$build_dir" --parallel 4
exec "$app_binary" "$@"
