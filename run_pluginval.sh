#!/bin/bash
#
# run_pluginval.sh — Build and validate Loop Breaker using pluginval
#
# Usage:
#   ./run_pluginval.sh                      # Default: Debug build, strictness 5
#   ./run_pluginval.sh --release            # Release build
#   ./run_pluginval.sh --strictness 10      # Max strictness
#   ./run_pluginval.sh --skip-build         # Skip build, just run validation
#   ./run_pluginval.sh --vst3               # Test VST3 instead of AU (macOS default is AU)
#
set -euo pipefail

# ─── Defaults ─────────────────────────────────────────────────────────────────
BUILD_TYPE="Debug"
STRICTNESS=5
SKIP_BUILD=false
FORMAT=""  # empty = use VST3 (AU requires system-level installation on macOS)

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/Builds/CMake"

# ─── Parse Arguments ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --release)
            BUILD_TYPE="Release"
            shift ;;
        --debug)
            BUILD_TYPE="Debug"
            shift ;;
        --strictness)
            STRICTNESS="$2"
            shift 2 ;;
        --skip-build)
            SKIP_BUILD=true
            shift ;;
        --vst3)
            FORMAT="VST3"
            shift ;;
        --au)
            FORMAT="AU"
            shift ;;
        -h|--help)
            head -11 "$0" | tail -8
            exit 0 ;;
        *)
            echo "Unknown option: $1"
            exit 1 ;;
    esac
done

# ─── Configure ────────────────────────────────────────────────────────────────
if [[ "$SKIP_BUILD" == false ]]; then
    # Locate cmake
    CMAKE_BIN="cmake"
    if ! command -v cmake &>/dev/null; then
        for p in /opt/homebrew/bin/cmake /usr/local/bin/cmake /Applications/CMake.app/Contents/bin/cmake; do
            if [[ -x "$p" ]]; then CMAKE_BIN="$p"; break; fi
        done
    fi

    echo "==> Configuring CMake ($BUILD_TYPE)..."
    "$CMAKE_BIN" -B "$BUILD_DIR" \
          -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
          -DPLUGINVAL_STRICTNESS_LEVEL="$STRICTNESS" \
          -DPLUGINVAL_VST3_VALIDATOR=OFF \
          "$PROJECT_ROOT"

    echo ""
    echo "==> Building pluginval + plugin..."
    "$CMAKE_BIN" --build "$BUILD_DIR" --config "$BUILD_TYPE" --target validate --parallel
fi

# ─── Resolve plugin path ─────────────────────────────────────────────────────
if [[ -z "$FORMAT" ]]; then
    FORMAT="VST3"
fi

if [[ "$FORMAT" == "AU" ]]; then
    PLUGIN_PATH="$BUILD_DIR/LoopBreaker_artefacts/$BUILD_TYPE/AU/Loop Breaker.component"
else
    PLUGIN_PATH="$BUILD_DIR/LoopBreaker_artefacts/$BUILD_TYPE/VST3/Loop Breaker.vst3"
fi

if [[ ! -e "$PLUGIN_PATH" ]]; then
    echo "ERROR: Plugin not found at: $PLUGIN_PATH"
    echo "Try running without --skip-build first."
    exit 1
fi

PLUGINVAL_BIN="$BUILD_DIR/Submodules/pluginval/pluginval_artefacts/$BUILD_TYPE/pluginval.app/Contents/MacOS/pluginval"
if [[ "$(uname)" != "Darwin" ]]; then
    PLUGINVAL_BIN="$BUILD_DIR/Submodules/pluginval/pluginval_artefacts/$BUILD_TYPE/pluginval"
fi

if [[ ! -x "$PLUGINVAL_BIN" ]]; then
    echo "ERROR: pluginval binary not found at: $PLUGINVAL_BIN"
    echo "Try running without --skip-build first."
    exit 1
fi

# ─── Run validation ──────────────────────────────────────────────────────────
echo ""
echo "==> Running pluginval (strictness $STRICTNESS) on: $PLUGIN_PATH"
echo ""

"$PLUGINVAL_BIN" --validate "$PLUGIN_PATH" --strictness-level "$STRICTNESS"

EXIT_CODE=$?
echo ""
if [[ $EXIT_CODE -eq 0 ]]; then
    echo "==> PASSED: All pluginval tests passed (strictness $STRICTNESS)"
else
    echo "==> FAILED: pluginval returned exit code $EXIT_CODE"
fi
exit $EXIT_CODE
