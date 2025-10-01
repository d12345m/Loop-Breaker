#!/bin/bash

# BufferTest - Build and Run Script
# This script builds the JUCE project and runs the application

set -e  # Exit on any error

MODE="run"
for arg in "$@"; do
    case "$arg" in
        --test|test) MODE="test" ; shift ;;
    esac
done

if [ "${TEST:-}" = "1" ]; then
    MODE="test"
fi

echo "🔨 Building BufferTest (mode: $MODE)..."
SCRIPT_DIR="$(dirname "$0")"
cd "$SCRIPT_DIR/Builds/MacOSX"

xcodebuild -project BufferTest.xcodeproj -configuration Debug -quiet || { echo "❌ Build failed!"; exit 1; }

APP_PATH="build/Debug/BufferTest.app"
BIN_PATH="$APP_PATH/Contents/MacOS/BufferTest"

if [ ! -f "$BIN_PATH" ]; then
    echo "❌ Built binary not found at $BIN_PATH"; exit 1
fi

if [ "$MODE" = "test" ]; then
    echo "🧪 Running tests..."
    "$BIN_PATH" --run-tests || { echo "❌ Tests failed."; exit 1; }
    echo "✅ Tests passed."
    exit 0
fi

echo "🚀 Launching BufferTest application..."
open "$APP_PATH"
echo "📱 BufferTest is now running!"
