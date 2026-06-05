#!/usr/bin/env bash
set -euo pipefail

build_dir="build_x86_64"
build_type="RelWithDebInfo"
package_root="package/linux/stream-saver"
output_dir="dist"
model_path=""
model_url=""
skip_build=0
skip_venv=0
skip_smoke=0
smoke_port=48743

usage() {
  cat <<'EOF'
Usage: scripts/package-release-linux.sh [options]

Options:
  --model-path PATH     Path to yolo11n-text.onnx to include in the package.
  --model-url URL       URL to download yolo11n-text.onnx from.
  --build-dir DIR       CMake build directory. Default: build_x86_64.
  --build-type TYPE     CMake build type. Default: RelWithDebInfo.
  --package-root DIR    Staging root. Default: package/linux/stream-saver.
  --output-dir DIR      Output directory. Default: dist.
  --smoke-port PORT     Worker smoke-test port. Default: 48743.
  --skip-build          Do not configure/build/install the C++ plugin.
  --skip-venv           Do not create/update the bundled worker venv.
  --skip-smoke          Do not smoke-test the bundled worker.
  -h, --help            Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --model-path)
      model_path="${2:?}"
      shift 2
      ;;
    --model-url)
      model_url="${2:?}"
      shift 2
      ;;
    --build-dir)
      build_dir="${2:?}"
      shift 2
      ;;
    --build-type)
      build_type="${2:?}"
      shift 2
      ;;
    --package-root)
      package_root="${2:?}"
      shift 2
      ;;
    --output-dir)
      output_dir="${2:?}"
      shift 2
      ;;
    --smoke-port)
      smoke_port="${2:?}"
      shift 2
      ;;
    --skip-build)
      skip_build=1
      shift
      ;;
    --skip-venv)
      skip_venv=1
      shift
      ;;
    --skip-smoke)
      skip_smoke=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root_dir"

install_root="package/linux-install"
worker_root="$package_root/data/worker"
release_root="package/linux-release"
zip_path="$output_dir/stream-saver-linux-x86_64.tar.gz"

if [[ "$skip_build" -eq 0 ]]; then
  cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type"
  cmake --build "$build_dir" --config "$build_type" --target stream-saver
  cmake --install "$build_dir" --config "$build_type" --prefix "$install_root"
fi

rm -rf "$package_root"
mkdir -p "$package_root/bin/64bit" "$package_root/data"

plugin_so="$(find "$install_root" -path '*/obs-plugins/stream-saver.so' -type f | head -n 1)"
if [[ -z "$plugin_so" ]]; then
  plugin_so="$(find "$build_dir" -name 'stream-saver.so' -type f | head -n 1)"
fi
if [[ -z "$plugin_so" ]]; then
  echo "stream-saver.so was not found. Build the Linux plugin before packaging." >&2
  exit 1
fi
cp "$plugin_so" "$package_root/bin/64bit/stream-saver.so"

if [[ -d "$install_root/share/obs/obs-plugins/stream-saver" ]]; then
  cp -a "$install_root/share/obs/obs-plugins/stream-saver/." "$package_root/data/"
else
  cp -a data/. "$package_root/data/"
fi

mkdir -p "$worker_root"
cp -a worker/. "$worker_root/"
rm -rf "$worker_root/__pycache__"
rm -f "$worker_root"/*.pyc "$worker_root"/stream-saver-last-*.png "$worker_root"/stream-saver-worker.log

model_target="$worker_root/yolo11n-text.onnx"
if [[ ! -f "$model_target" ]]; then
  if [[ -n "$model_path" ]]; then
    cp "$model_path" "$model_target"
  elif [[ -n "$model_url" ]]; then
    if command -v curl >/dev/null 2>&1; then
      curl -L "$model_url" -o "$model_target"
    elif command -v wget >/dev/null 2>&1; then
      wget -O "$model_target" "$model_url"
    else
      echo "Need curl or wget to download the model." >&2
      exit 1
    fi
  else
    cat >&2 <<EOF
Missing yolo11n-text.onnx.

Pass --model-path /path/to/yolo11n-text.onnx, pass --model-url, or place
yolo11n-text.onnx in $worker_root before packaging.
EOF
    exit 1
  fi
fi

if [[ "$skip_venv" -eq 0 ]]; then
  python3 -m venv --copies "$worker_root/python"
  "$worker_root/python/bin/python" -m pip install --upgrade pip
  "$worker_root/python/bin/python" -m pip install -r worker/requirements-linux.txt
fi

cat > "$worker_root/stream-saver-ocr" <<'EOF'
#!/usr/bin/env sh
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if [ -x "$SCRIPT_DIR/python/bin/python" ]; then
  exec "$SCRIPT_DIR/python/bin/python" "$SCRIPT_DIR/stream_saver_ocr.py" "$@"
fi
exec python3 "$SCRIPT_DIR/stream_saver_ocr.py" "$@"
EOF
chmod +x "$worker_root/stream-saver-ocr"

if [[ "$skip_smoke" -eq 0 ]]; then
  worker_out="build/release-worker-smoke-linux.out.log"
  worker_err="build/release-worker-smoke-linux.err.log"
  mkdir -p build
  rm -f "$worker_out" "$worker_err"
  "$worker_root/python/bin/python" "$worker_root/stream_saver_ocr.py" \
    --port "$smoke_port" \
    --backend onnxruntime \
    --model "$model_target" \
    --no-recognize >"$worker_out" 2>"$worker_err" &
  worker_pid=$!
  cleanup() {
    if kill -0 "$worker_pid" >/dev/null 2>&1; then
      kill "$worker_pid" >/dev/null 2>&1 || true
      wait "$worker_pid" >/dev/null 2>&1 || true
    fi
  }
  trap cleanup EXIT
  sleep 8
  if ! kill -0 "$worker_pid" >/dev/null 2>&1; then
    cat "$worker_err" >&2 || true
    cat "$worker_out" >&2 || true
    echo "Release smoke worker exited before the smoke test." >&2
    exit 1
  fi
  "$worker_root/python/bin/python" scripts/ocr_smoke.py "$smoke_port"
  cleanup
  trap - EXIT
fi

mkdir -p "$output_dir"
rm -rf "$release_root"
mkdir -p "$release_root"
cp -a "$package_root" "$release_root/stream-saver"

cat > "$release_root/install.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'HELP'
Usage: ./install.sh [options]

Options:
  --native       Install for native OBS at ~/.config/obs-studio/plugins.
  --flatpak      Install for Flatpak OBS at ~/.var/app/com.obsproject.Studio/config/obs-studio/plugins.
  --dir PATH     Install into a custom OBS plugins directory.
  -h, --help     Show this help.
HELP
}

mode="auto"
custom_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --native)
      mode="native"
      shift
      ;;
    --flatpak)
      mode="flatpak"
      shift
      ;;
    --dir)
      custom_dir="${2:?}"
      mode="custom"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
plugin_src="$script_dir/stream-saver"

if [[ ! -f "$plugin_src/bin/64bit/stream-saver.so" ]]; then
  echo "stream-saver plugin files were not found next to install.sh." >&2
  exit 1
fi

native_config="${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio"
flatpak_config="$HOME/.var/app/com.obsproject.Studio/config/obs-studio"

case "$mode" in
  native)
    plugins_dir="$native_config/plugins"
    ;;
  flatpak)
    plugins_dir="$flatpak_config/plugins"
    ;;
  custom)
    plugins_dir="$custom_dir"
    ;;
  auto)
    if [[ -d "$native_config" ]]; then
      plugins_dir="$native_config/plugins"
    elif [[ -d "$flatpak_config" ]]; then
      plugins_dir="$flatpak_config/plugins"
    else
      plugins_dir="$native_config/plugins"
      echo "OBS config directory was not found; creating native OBS plugin path:"
      echo "  $plugins_dir"
    fi
    ;;
  *)
    echo "Unknown install mode: $mode" >&2
    exit 2
    ;;
esac

if command -v pgrep >/dev/null 2>&1; then
  if pgrep -x obs >/dev/null 2>&1 || pgrep -x obs-studio >/dev/null 2>&1; then
    echo "OBS appears to be running. Close OBS before restarting it to load the new plugin."
  fi
fi

mkdir -p "$plugins_dir"
target="$plugins_dir/stream-saver"

if [[ -e "$target" ]]; then
  backup="$target.backup.$(date +%Y%m%d-%H%M%S)"
  mv "$target" "$backup"
  echo "Existing Stream Saver plugin backed up to:"
  echo "  $backup"
fi

cp -a "$plugin_src" "$target"

echo "Stream Saver installed to:"
echo "  $target"
echo
echo "Restart OBS, then add Stream Saver as an Effect Filter."
EOF
chmod +x "$release_root/install.sh"

tar -C "$release_root" -czf "$zip_path" install.sh stream-saver

echo "Release package created: $zip_path"
echo "Users can extract it, then run ./install.sh."
