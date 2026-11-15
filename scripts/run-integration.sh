#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"$repo_dir/build"}
build_dir=$(realpath -- "$build_dir")
# Staging is only a test fixture; it does not install a desktop widget.
DESTDIR="$build_dir/test-root" cmake --install "$build_dir"
mkdir -p "$build_dir/test-config" "$build_dir/test-cache"
export XDG_CONFIG_HOME="$build_dir/test-config"
export XDG_CACHE_HOME="$build_dir/test-cache"
export XDG_DATA_DIRS="$build_dir/test-root/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export QT_PLUGIN_PATH="$build_dir/lib${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
"$build_dir/stock_integration"
"$build_dir/companion_integration"
