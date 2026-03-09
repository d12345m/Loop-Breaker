#!/bin/bash
#
# build_installer.sh — Build a macOS .pkg installer for Loop Breaker (VST3 + AU)
#
# Usage:
#   ./Installer/build_installer.sh [--skip-build] [--sign "Developer ID Installer: ..."]
#
# Options:
#   --skip-build   Skip the Xcode build step (use existing Release build)
#   --sign ID      Code-sign the .pkg with the given Developer ID Installer identity
#   --notarize     Submit the signed .pkg for Apple notarization (requires --sign)
#
set -euo pipefail

# ─── Configuration ────────────────────────────────────────────────────────────
PLUGIN_NAME="Loop Breaker"
PLUGIN_FILENAME="LoopBreaker"                      # .vst3 bundle name
BUNDLE_ID="com.glowmachineaudio.LoopBreaker"
COMPANY="GlowMachineAudio"
VERSION="1.0.0"

# Paths (relative to project root)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
XCODE_PROJECT="$PROJECT_ROOT/Builds/MacOSX/${PLUGIN_FILENAME}.xcodeproj"
BUILD_DIR="$PROJECT_ROOT/Builds/MacOSX/build/Release"
VST3_BUNDLE="$BUILD_DIR/${PLUGIN_FILENAME}.vst3"
AU_BUNDLE="$BUILD_DIR/${PLUGIN_FILENAME}.component"

INSTALLER_DIR="$SCRIPT_DIR"
RESOURCES_DIR="$INSTALLER_DIR/resources"
STAGING_DIR="$INSTALLER_DIR/staging"
OUTPUT_DIR="$INSTALLER_DIR/output"
VST3_COMPONENT_PKG="$STAGING_DIR/LoopBreakerVST3.pkg"
AU_COMPONENT_PKG="$STAGING_DIR/LoopBreakerAU.pkg"
DISTRIBUTION_XML="$INSTALLER_DIR/distribution.xml"

SKIP_BUILD=false
SIGN_IDENTITY=""
NOTARIZE=false

# ─── Parse Arguments ─────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --sign)
            SIGN_IDENTITY="$2"
            shift 2
            ;;
        --notarize)
            NOTARIZE=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# ─── Helpers ──────────────────────────────────────────────────────────────────
info()  { echo "✅  $*"; }
warn()  { echo "⚠️   $*"; }
fail()  { echo "❌  $*" >&2; exit 1; }

cleanup() {
    rm -rf "$STAGING_DIR"
}
trap cleanup EXIT

# ─── Step 1: Build Release ───────────────────────────────────────────────────
if [ "$SKIP_BUILD" = false ]; then
    info "Building ${PLUGIN_NAME} VST3 (Release)..."
    cd "$PROJECT_ROOT/Builds/MacOSX"
    xcodebuild \
        -project "${PLUGIN_FILENAME}.xcodeproj" \
        -scheme "${PLUGIN_FILENAME} - VST3" \
        -configuration Release \
        build \
        2>&1 | tail -5

    info "Building ${PLUGIN_NAME} AU (Release)..."
    xcodebuild \
        -project "${PLUGIN_FILENAME}.xcodeproj" \
        -scheme "${PLUGIN_FILENAME} - AU" \
        -configuration Release \
        build \
        2>&1 | tail -5
    cd "$PROJECT_ROOT"
else
    info "Skipping build (--skip-build)"
fi

# Verify the VST3 bundle exists
if [ ! -d "$VST3_BUNDLE" ]; then
    fail "VST3 bundle not found at: $VST3_BUNDLE"
fi
info "Found VST3 bundle: $VST3_BUNDLE"

# Verify the AU bundle exists
if [ ! -d "$AU_BUNDLE" ]; then
    fail "AU bundle not found at: $AU_BUNDLE"
fi
info "Found AU bundle: $AU_BUNDLE"

# ─── Step 2: Prepare Staging Area ────────────────────────────────────────────
info "Preparing staging area..."
rm -rf "$STAGING_DIR" "$OUTPUT_DIR"
mkdir -p "$STAGING_DIR/vst3-payload/Library/Audio/Plug-Ins/VST3"
mkdir -p "$STAGING_DIR/au-payload/Library/Audio/Plug-Ins/Components"
mkdir -p "$OUTPUT_DIR"

# Copy the VST3 bundle into the payload
cp -R "$VST3_BUNDLE" "$STAGING_DIR/vst3-payload/Library/Audio/Plug-Ins/VST3/"
info "Staged VST3 to /Library/Audio/Plug-Ins/VST3/${PLUGIN_FILENAME}.vst3"

# Copy the AU bundle into the payload
cp -R "$AU_BUNDLE" "$STAGING_DIR/au-payload/Library/Audio/Plug-Ins/Components/"
info "Staged AU to /Library/Audio/Plug-Ins/Components/${PLUGIN_FILENAME}.component"

# ─── Step 3: Build Component Package ─────────────────────────────────────────
info "Building VST3 component package..."
pkgbuild \
    --root "$STAGING_DIR/vst3-payload" \
    --identifier "${BUNDLE_ID}.vst3" \
    --version "$VERSION" \
    --install-location "/" \
    "$VST3_COMPONENT_PKG"

info "Building AU component package..."
pkgbuild \
    --root "$STAGING_DIR/au-payload" \
    --identifier "${BUNDLE_ID}.au" \
    --version "$VERSION" \
    --install-location "/" \
    "$AU_COMPONENT_PKG"

# ─── Step 4: Generate Distribution XML ───────────────────────────────────────
# The distribution.xml is checked into the repo, but we regenerate a version-
# stamped copy in the staging dir if the template doesn't exist yet.
if [ ! -f "$DISTRIBUTION_XML" ]; then
    warn "distribution.xml not found — generating default"
    cat > "$DISTRIBUTION_XML" <<DISTEOF
<?xml version="1.0" encoding="UTF-8"?>
<installer-gui-script minSpecVersion="2">
    <title>${PLUGIN_NAME}</title>
    <organization>${BUNDLE_ID}</organization>
    <options customize="never" require-scripts="false" hostArchitectures="x86_64,arm64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <welcome    file="welcome.html"/>
    <conclusion file="conclusion.html"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="vst3" title="${PLUGIN_NAME} VST3">
        <pkg-ref id="${BUNDLE_ID}.vst3"/>
    </choice>
    <choice id="au" title="${PLUGIN_NAME} AU">
        <pkg-ref id="${BUNDLE_ID}.au"/>
    </choice>
    <pkg-ref id="${BUNDLE_ID}.vst3" version="${VERSION}" onConclusion="none">LoopBreakerVST3.pkg</pkg-ref>
    <pkg-ref id="${BUNDLE_ID}.au" version="${VERSION}" onConclusion="none">LoopBreakerAU.pkg</pkg-ref>
</installer-gui-script>
DISTEOF
fi

# ─── Step 5: Build Product Archive ───────────────────────────────────────────
FINAL_PKG="$STAGING_DIR/LoopBreaker-macOS.pkg"

info "Building product installer..."
PRODUCTBUILD_ARGS=(
    --distribution "$DISTRIBUTION_XML"
    --resources "$RESOURCES_DIR"
    --package-path "$STAGING_DIR"
    "$FINAL_PKG"
)

if [ -n "$SIGN_IDENTITY" ]; then
    PRODUCTBUILD_ARGS+=(--sign "$SIGN_IDENTITY")
    info "Signing with: $SIGN_IDENTITY"
fi

productbuild "${PRODUCTBUILD_ARGS[@]}"

# ─── Step 6: Optional Notarization ───────────────────────────────────────────
if [ "$NOTARIZE" = true ]; then
    if [ -z "$SIGN_IDENTITY" ]; then
        fail "Notarization requires --sign"
    fi
    info "Submitting for notarization..."
    xcrun notarytool submit "$FINAL_PKG" \
        --keychain-profile "notarization-profile" \
        --wait
    info "Stapling notarization ticket..."
    xcrun stapler staple "$FINAL_PKG"
fi

# ─── Step 7: Generate Manual ──────────────────────────────────────────────────
info "Generating user manual PDF..."
MANUAL_SCRIPT="$PROJECT_ROOT/generate_manual.py"
MANUAL_PDF="$PROJECT_ROOT/Loop_Breaker_User_Manual.pdf"
if [ -f "$MANUAL_SCRIPT" ]; then
    python3 "$MANUAL_SCRIPT"
    if [ ! -f "$MANUAL_PDF" ]; then
        warn "Manual generation did not produce expected PDF at: $MANUAL_PDF"
    fi
else
    warn "Manual script not found at: $MANUAL_SCRIPT — skipping manual"
fi

# ─── Step 8: Create Installer Distribution Zip ───────────────────────────────
ZIP_STAGING="$STAGING_DIR/Loop Breaker"
ZIP_FILE="$OUTPUT_DIR/LoopBreaker-macOS-Installer.zip"

info "Creating installer distribution zip..."
mkdir -p "$ZIP_STAGING"
cp "$FINAL_PKG" "$ZIP_STAGING/"
if [ -f "$MANUAL_PDF" ]; then
    cp "$MANUAL_PDF" "$ZIP_STAGING/"
    info "Included manual in installer zip"
fi

# Create zip from staging dir so it unzips to "Loop Breaker/"
(cd "$STAGING_DIR" && zip -r "$ZIP_FILE" "Loop Breaker")
info "Created installer zip: $ZIP_FILE"

# ─── Step 9: Create Standalone VST3 Distribution Zip ─────────────────────────
VST3_ZIP_STAGING="$STAGING_DIR/Loop Breaker VST3"
VST3_ZIP_FILE="$OUTPUT_DIR/LoopBreaker-VST3.zip"

info "Creating standalone VST3 distribution zip..."
mkdir -p "$VST3_ZIP_STAGING"

# Copy the raw VST3 bundle
cp -R "$VST3_BUNDLE" "$VST3_ZIP_STAGING/"
info "Included VST3 bundle in standalone zip"

# Copy the user manual
if [ -f "$MANUAL_PDF" ]; then
    cp "$MANUAL_PDF" "$VST3_ZIP_STAGING/"
    info "Included manual in standalone zip"
fi

# Generate the installation instructions text file
INSTALL_GUIDE="$VST3_ZIP_STAGING/INSTALLATION.txt"
cat > "$INSTALL_GUIDE" <<'INSTALLEOF'
==============================================================================
  LOOP BREAKER — VST3 Manual Installation Guide
==============================================================================

This archive contains the Loop Breaker VST3 plugin for manual installation.
If you are on macOS and prefer an automated install, use the separate
LoopBreaker-macOS-Installer.zip package instead.


------------------------------------------------------------------------------
  CONTENTS
------------------------------------------------------------------------------

  LoopBreaker.vst3                  The VST3 plugin bundle
  Loop_Breaker_User_Manual.pdf      User manual
  INSTALLATION.txt                  This file


==============================================================================
  macOS INSTALLATION
==============================================================================

1. Copy the "LoopBreaker.vst3" bundle to one of these locations:

   For all users (requires admin password):
     /Library/Audio/Plug-Ins/VST3/

   For your user account only:
     ~/Library/Audio/Plug-Ins/VST3/

   Tip: The ~/Library folder is hidden by default. In Finder, press
   Cmd+Shift+G and paste the path above, or hold Option while clicking
   the "Go" menu to reveal "Library".

2. Open your DAW and rescan plugins if necessary. Loop Breaker should
   appear in your plugin list under the manufacturer "Glow Machine".


  ALLOWING AN UNSIGNED PLUGIN ON macOS
  -------------------------------------

  Because Loop Breaker is not currently code-signed or notarized by Apple,
  macOS Gatekeeper will block it the first time you try to use it.
  To allow it:

  macOS Sequoia 15 and later:
    1. When your DAW fails to load the plugin (or you see a dialog saying
       the file "can't be opened"), dismiss the dialog.
    2. Open System Settings > Privacy & Security.
    3. Scroll down. You should see a message like:
       "LoopBreaker.vst3 was blocked from use because it is not from an
       identified developer."
    4. Click "Allow Anyway" and authenticate with your password or
       Touch ID.
    5. Re-open your DAW or rescan plugins. When prompted again, click
       "Open" to confirm.

  macOS Ventura 13 / Sonoma 14:
    Same steps as above — System Settings > Privacy & Security > Allow
    Anyway.

  macOS Monterey 12 and earlier:
    1. Open System Preferences > Security & Privacy > General tab.
    2. You should see "LoopBreaker.vst3 was blocked..."
    3. Click "Allow Anyway" and authenticate.
    4. Re-open your DAW and confirm when prompted.

  Alternative (Terminal):
    If the above does not work, you can remove the quarantine flag
    manually. Open Terminal and run:

      xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/LoopBreaker.vst3

    (Adjust the path if you installed to ~/Library/... instead.)


==============================================================================
  WINDOWS INSTALLATION
==============================================================================

1. Copy the "LoopBreaker.vst3" file to:

     C:\Program Files\Common Files\VST3\

   You may need administrator privileges to copy files into this folder.

2. Open your DAW and rescan plugins if necessary. Loop Breaker should
   appear in your plugin list under the manufacturer "Glow Machine".

   Note: Some DAWs also support a per-user VST3 folder. Check your
   DAW's documentation if the system-wide folder does not work for
   your setup.


==============================================================================
  TROUBLESHOOTING
==============================================================================

  - Plugin not showing up?
    Make sure the .vst3 file/bundle is in the correct folder for your
    OS (see above) and rescan plugins in your DAW.

  - macOS still blocking the plugin?
    Try the Terminal method described above (xattr -dr ...).

  - DAW crashes on load?
    Ensure you are using a compatible DAW that supports the VST3 format
    and that your OS version is supported.

  - Need help?
    Visit https://glowmachineaudio.com/support


==============================================================================
  Copyright (c) Glow Machine, LLC. All rights reserved.
==============================================================================
INSTALLEOF
info "Generated installation guide: INSTALLATION.txt"

# Create the standalone VST3 zip
(cd "$STAGING_DIR" && zip -r "$VST3_ZIP_FILE" "Loop Breaker VST3")
info "Created standalone VST3 zip: $VST3_ZIP_FILE"

# ─── Done ─────────────────────────────────────────────────────────────────────
INSTALLER_SIZE=$(du -sh "$ZIP_FILE" | cut -f1)
VST3_SIZE=$(du -sh "$VST3_ZIP_FILE" | cut -f1)
info "Build complete!"
echo ""
echo "  📦  $ZIP_FILE  ($INSTALLER_SIZE)"
echo "       macOS installer package + manual"
echo ""
echo "  🎛   $VST3_ZIP_FILE  ($VST3_SIZE)"
echo "       Standalone VST3 + installation guide + manual"
echo ""
echo "  Install location: /Library/Audio/Plug-Ins/VST3/${PLUGIN_FILENAME}.vst3"
echo ""
if [ -z "$SIGN_IDENTITY" ]; then
    warn "Package is unsigned. For distribution, re-run with:"
    echo "     --sign \"Developer ID Installer: ${COMPANY} (TEAM_ID)\""
fi
