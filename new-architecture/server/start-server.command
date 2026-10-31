#!/bin/zsh
set -euo pipefail

# Double-click this file in Finder, or run it from the project root.
export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/sbin:/usr/bin:/bin:/sbin:$PATH"
unset QT_QPA_PLATFORM

script_dir="${0:A:h}"
build_dir="$script_dir/build"
app_binary="$build_dir/bohr-qt-server.app/Contents/MacOS/bohr-qt-server"
database_file="$HOME/Library/Application Support/Boer Qt Server/boer-lan-qt.db"

if [[ "${DB_TYPE:-sqlite}" != "mysql" && ! -f "$database_file" ]]; then
  print -u2 "正式数据库不存在：$database_file"
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  print -u2 "找不到 CMake，请先安装 CMake。"
  exit 1
fi

if [[ ! -f "$build_dir/CMakeCache.txt" ]]; then
  cmake -S "$script_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$build_dir" --config Release --parallel 4

if command -v lsof >/dev/null 2>&1; then
  tcp_listener="$(lsof -nP -iTCP:38400 -sTCP:LISTEN 2>/dev/null || true)"
  if [[ -n "$tcp_listener" ]]; then
    if [[ "$tcp_listener" == *boer-qt* || "$tcp_listener" == *bohr-qt* ]]; then
      print "Qt 服务端已经在运行（设备端口 38400）。"
      exit 0
    fi
    print -u2 "设备端口 38400 已被其他程序占用："
    print -u2 "$tcp_listener"
    exit 1
  fi
fi

print "启动正式 Qt 服务端，数据库：$database_file"
if "$app_binary" --db "$database_file" --tcp-port 38400 "$@"; then
  exit 0
else
  status=$?
  log_file="${database_file:h}/logs/server_$(date +%Y-%m-%d).log"
  print -u2 "服务端启动失败（退出码 $status）。"
  if [[ -f "$log_file" ]]; then
    print -u2 "最近日志："
    tail -n 5 "$log_file" >&2
  fi
  exit "$status"
fi
