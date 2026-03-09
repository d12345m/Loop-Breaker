#!/bin/bash
#
# uninstall.sh — Remove Loop Breaker from macOS
#
set -euo pipefail

VST3_PATH="/Library/Audio/Plug-Ins/VST3/LoopBreaker.vst3"
AU_PATH="/Library/Audio/Plug-Ins/Components/LoopBreaker.component"
PKG_ID_VST3="com.glowmachineaudio.LoopBreaker.vst3"
PKG_ID_AU="com.glowmachineaudio.LoopBreaker.au"

echo "Loop Breaker Uninstaller"
echo "========================"
echo ""

FOUND=false
if [ -d "$VST3_PATH" ]; then FOUND=true; fi
if [ -d "$AU_PATH" ]; then FOUND=true; fi

if [ "$FOUND" = false ]; then
    echo "Loop Breaker not found at expected locations."
    echo "Nothing to uninstall."
    exit 0
fi

echo "This will remove:"
[ -d "$VST3_PATH" ] && echo "  - $VST3_PATH"
[ -d "$AU_PATH" ] && echo "  - $AU_PATH"
echo ""
read -p "Continue? [y/N] " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 0
fi

if [ -d "$VST3_PATH" ]; then
    echo "Removing VST3..."
    sudo rm -rf "$VST3_PATH"
fi

if [ -d "$AU_PATH" ]; then
    echo "Removing AU..."
    sudo rm -rf "$AU_PATH"
fi

# Forget the package receipts
if pkgutil --pkg-info "$PKG_ID_VST3" &>/dev/null; then
    echo "Removing VST3 package receipt..."
    sudo pkgutil --forget "$PKG_ID_VST3"
fi
if pkgutil --pkg-info "$PKG_ID_AU" &>/dev/null; then
    echo "Removing AU package receipt..."
    sudo pkgutil --forget "$PKG_ID_AU"
fi

echo ""
echo "✅ Loop Breaker has been removed."
echo "   You may need to rescan plug-ins in your DAW."
