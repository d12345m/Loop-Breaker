#!/bin/bash

# BufferTest - Build Script (VST3)
# This script builds the VST3 plugin target.

set -e  # Exit on any error

echo "🔨 Building BufferTest (VST3, Debug)..."
SCRIPT_DIR="$(dirname "$0")"
cd "$SCRIPT_DIR/Builds/MacOSX"

xcodebuild -project BufferTest.xcodeproj -scheme "BufferTest - VST3" -configuration Debug build -quiet || { echo "❌ Build failed!"; exit 1; }

VST3_PATH="build/Debug/BufferTest.vst3"
if [ -d "$VST3_PATH" ]; then
    echo "✅ Built: $SCRIPT_DIR/Builds/MacOSX/$VST3_PATH"
else
    echo "⚠️  Build succeeded but VST3 bundle not found at $VST3_PATH"
fi

INSTALLED="$HOME/Library/Audio/Plug-Ins/VST3/BufferTest.vst3"
if [ -d "$INSTALLED" ]; then
    echo "✅ Installed: $INSTALLED"
else
    echo "ℹ️  Not installed at: $INSTALLED"
fi
