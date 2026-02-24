#!/bin/bash
#
# uninstall.sh — Remove Loop Breaker from macOS
#
set -euo pipefail

VST3_PATH="/Library/Audio/Plug-Ins/VST3/TestPlugin.vst3"
PKG_ID="com.glowmachineaudio.LoopBreaker"

echo "Loop Breaker Uninstaller"
echo "========================"
echo ""

if [ ! -d "$VST3_PATH" ]; then
    echo "Loop Breaker VST3 not found at $VST3_PATH"
    echo "Nothing to uninstall."
    exit 0
fi

echo "This will remove:"
echo "  - $VST3_PATH"
echo ""
read -p "Continue? [y/N] " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 0
fi

echo "Removing VST3..."
sudo rm -rf "$VST3_PATH"

# Forget the package receipt
if pkgutil --pkg-info "$PKG_ID" &>/dev/null; then
    echo "Removing package receipt..."
    sudo pkgutil --forget "$PKG_ID"
fi

echo ""
echo "✅ Loop Breaker has been removed."
echo "   You may need to rescan plug-ins in your DAW."
