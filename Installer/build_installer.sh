#!/bin/bash
#
# build_installer.sh — Build a macOS .pkg installer for Loop Breaker VST3
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

INSTALLER_DIR="$SCRIPT_DIR"
RESOURCES_DIR="$INSTALLER_DIR/resources"
STAGING_DIR="$INSTALLER_DIR/staging"
OUTPUT_DIR="$INSTALLER_DIR/output"
COMPONENT_PKG="$STAGING_DIR/LoopBreakerVST3.pkg"
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
    info "Building ${PLUGIN_NAME} (Release)..."
    cd "$PROJECT_ROOT/Builds/MacOSX"
    xcodebuild \
        -project "${PLUGIN_FILENAME}.xcodeproj" \
        -scheme "${PLUGIN_FILENAME} - VST3" \
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

# ─── Step 2: Prepare Staging Area ────────────────────────────────────────────
info "Preparing staging area..."
rm -rf "$STAGING_DIR" "$OUTPUT_DIR"
mkdir -p "$STAGING_DIR/payload/Library/Audio/Plug-Ins/VST3"
mkdir -p "$OUTPUT_DIR"

# Copy the VST3 bundle into the payload
cp -R "$VST3_BUNDLE" "$STAGING_DIR/payload/Library/Audio/Plug-Ins/VST3/"
info "Staged VST3 to /Library/Audio/Plug-Ins/VST3/${PLUGIN_FILENAME}.vst3"

# ─── Step 3: Build Component Package ─────────────────────────────────────────
info "Building component package..."
pkgbuild \
    --root "$STAGING_DIR/payload" \
    --identifier "$BUNDLE_ID" \
    --version "$VERSION" \
    --install-location "/" \
    "$COMPONENT_PKG"

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
        <line choice="default"/>
    </choices-outline>
    <choice id="default" title="${PLUGIN_NAME} VST3">
        <pkg-ref id="${BUNDLE_ID}"/>
    </choice>
    <pkg-ref id="${BUNDLE_ID}" version="${VERSION}" onConclusion="none">LoopBreakerVST3.pkg</pkg-ref>
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

# ─── Step 8: Create Distribution Zip ─────────────────────────────────────────
ZIP_STAGING="$STAGING_DIR/Loop Breaker"
ZIP_FILE="$OUTPUT_DIR/LoopBreaker-macOS.zip"

info "Creating distribution zip..."
mkdir -p "$ZIP_STAGING"
cp "$FINAL_PKG" "$ZIP_STAGING/"
if [ -f "$MANUAL_PDF" ]; then
    cp "$MANUAL_PDF" "$ZIP_STAGING/"
    info "Included manual in zip"
fi

# Create zip from staging dir so it unzips to "Loop Breaker/"
(cd "$STAGING_DIR" && zip -r "$ZIP_FILE" "Loop Breaker")
info "Created zip: $ZIP_FILE"

# ─── Done ─────────────────────────────────────────────────────────────────────
ZIP_SIZE=$(du -sh "$ZIP_FILE" | cut -f1)
info "Installer built successfully!"
echo ""
echo "  🗜   $ZIP_FILE  ($ZIP_SIZE)"
echo ""
echo "  Install location: /Library/Audio/Plug-Ins/VST3/${PLUGIN_FILENAME}.vst3"
echo ""
if [ -z "$SIGN_IDENTITY" ]; then
    warn "Package is unsigned. For distribution, re-run with:"
    echo "     --sign \"Developer ID Installer: ${COMPANY} (TEAM_ID)\""
fi
