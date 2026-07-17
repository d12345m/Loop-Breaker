#!/usr/bin/env bash
# bump_version.sh — increment the release version stored in VERSION.
#
# Usage:
#   ./bump_version.sh patch   (1.0.0 -> 1.0.1)
#   ./bump_version.sh minor   (1.0.1 -> 1.1.0)
#   ./bump_version.sh major   (1.1.0 -> 2.0.0)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION_FILE="$SCRIPT_DIR/VERSION"

if [[ ! -f "$VERSION_FILE" ]]; then
    echo "ERROR: VERSION file not found at $VERSION_FILE" >&2
    exit 1
fi

BUMP_TYPE="${1:-patch}"
CURRENT=$(cat "$VERSION_FILE" | tr -d '[:space:]')

IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT"

case "$BUMP_TYPE" in
    major)
        MAJOR=$((MAJOR + 1))
        MINOR=0
        PATCH=0
        ;;
    minor)
        MINOR=$((MINOR + 1))
        PATCH=0
        ;;
    patch)
        PATCH=$((PATCH + 1))
        ;;
    *)
        echo "Usage: $0 [major|minor|patch]" >&2
        exit 1
        ;;
esac

NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
echo "$NEW_VERSION" > "$VERSION_FILE"
echo "[bump_version] $CURRENT -> $NEW_VERSION"

# Regenerate BuildInfo.h with the new version and current timestamp
bash "$SCRIPT_DIR/gen_build_info.sh"
