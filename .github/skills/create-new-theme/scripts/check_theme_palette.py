#!/usr/bin/env python3
"""Audit one ThemePalette factory in Loop Breaker's ThemeEngine.cpp."""

from __future__ import annotations

import argparse
import colorsys
import re
import sys
from pathlib import Path


REQUIRED_COLOURS = {
    "bg", "bgAlt", "panel", "panelAlt",
    "border", "borderGlow",
    "textPrimary", "textSecondary", "textOnAccent",
    "accent1", "accent2", "accent3",
    "good", "warn", "bad",
    "knobFill", "knobTrack", "waveformFill", "playhead",
    "padEmpty", "padLoaded", "padSelected", "padSelectedIndicator",
    "padFlash", "padPlaying",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check completeness, contrast, and pad-state visibility for a ThemePalette factory."
    )
    parser.add_argument("source", type=Path, help="Path to Source/ThemeEngine.cpp")
    parser.add_argument("factory", help="Factory function name, for example makeMarathonAcid")
    return parser.parse_args()


def rgb(hex_value: str, alpha: float = 1.0) -> tuple[float, float, float, float]:
    value = int(hex_value, 16)
    return (
        ((value >> 16) & 0xFF) / 255.0,
        ((value >> 8) & 0xFF) / 255.0,
        (value & 0xFF) / 255.0,
        alpha,
    )


def extract_factory(source: str, name: str) -> str:
    match = re.search(
        rf"(?:\[\[maybe_unused\]\]\]\s*)?static\s+ThemePalette\s+{re.escape(name)}"
        rf"\s*\(\s*\)\s*\{{(?P<body>.*?)\n\}}",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ValueError(f"could not find ThemePalette factory {name!r}")
    return match.group("body")


def parse_colours(body: str) -> dict[str, tuple[float, float, float, float]]:
    colours: dict[str, tuple[float, float, float, float]] = {}
    aliases: dict[str, str] = {}

    for field, value, alpha in re.findall(
        r"p\.(\w+)\s*=\s*hexA\s*\(\s*0x([0-9A-Fa-f]{6})\s*,\s*([0-9.]+)f?\s*\)\s*;",
        body,
    ):
        colours[field] = rgb(value, float(alpha))

    for field, value in re.findall(
        r"p\.(\w+)\s*=\s*hex\s*\(\s*0x([0-9A-Fa-f]{6})\s*\)\s*;",
        body,
    ):
        colours[field] = rgb(value)

    for field, target in re.findall(r"p\.(\w+)\s*=\s*p\.(\w+)\s*;", body):
        aliases[field] = target

    unresolved = dict(aliases)
    while unresolved:
        progress = False
        for field, target in list(unresolved.items()):
            if target in colours:
                colours[field] = colours[target]
                del unresolved[field]
                progress = True
        if not progress:
            detail = ", ".join(f"{field}->{target}" for field, target in unresolved.items())
            raise ValueError(f"unresolved colour aliases: {detail}")

    return colours


def channel_luminance(value: float) -> float:
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def luminance(colour: tuple[float, float, float, float]) -> float:
    red, green, blue, _ = colour
    return (
        0.2126 * channel_luminance(red)
        + 0.7152 * channel_luminance(green)
        + 0.0722 * channel_luminance(blue)
    )


def contrast(
    foreground: tuple[float, float, float, float],
    background: tuple[float, float, float, float],
) -> float:
    lighter = max(luminance(foreground), luminance(background))
    darker = min(luminance(foreground), luminance(background))
    return (lighter + 0.05) / (darker + 0.05)


def composite(
    overlay: tuple[float, float, float, float],
    base: tuple[float, float, float, float],
) -> tuple[float, float, float, float]:
    alpha = overlay[3]
    return tuple(
        overlay[index] * alpha + base[index] * (1.0 - alpha)
        for index in range(3)
    ) + (1.0,)


def main() -> int:
    args = parse_args()

    try:
        body = extract_factory(args.source.read_text(encoding="utf-8"), args.factory)
        colours = parse_colours(body)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    failures: list[str] = []
    missing = sorted(REQUIRED_COLOURS - colours.keys())
    if missing:
        failures.append("missing colour roles: " + ", ".join(missing))

    def require_contrast(foreground: str, background: str, minimum: float) -> None:
        if foreground not in colours or background not in colours:
            return
        ratio = contrast(colours[foreground], colours[background])
        status = "PASS" if ratio >= minimum else "FAIL"
        print(f"{status} contrast {foreground}/{background}: {ratio:.2f}:1 (minimum {minimum:.1f}:1)")
        if ratio < minimum:
            failures.append(
                f"{foreground}/{background} contrast is {ratio:.2f}:1; expected at least {minimum:.1f}:1"
            )

    for surface in ("bg", "bgAlt", "panel", "panelAlt"):
        require_contrast("textPrimary", surface, 4.5)
        require_contrast("textSecondary", surface, 4.5)
    require_contrast("textSecondary", "padEmpty", 4.5)
    require_contrast("textOnAccent", "accent1", 4.5)
    require_contrast("accent1", "bgAlt", 3.0)
    require_contrast("border", "padEmpty", 3.0)
    require_contrast("waveformFill", "padLoaded", 3.0)
    require_contrast("playhead", "padLoaded", 3.0)

    if {"padSelected", "padEmpty", "padLoaded"} <= colours.keys():
        for surface in ("padEmpty", "padLoaded"):
            before = luminance(colours[surface])
            after = luminance(composite(colours["padSelected"], colours[surface]))
            status = "PASS" if after > before else "FAIL"
            print(f"{status} padSelected brightens {surface}: {before:.4f} -> {after:.4f}")
            if after <= before:
                failures.append(f"padSelected does not brighten {surface}")

    if "padFlash" in colours:
        saturation = colorsys.rgb_to_hsv(*colours["padFlash"][:3])[1]
        status = "PASS" if saturation >= 0.5 else "WARN"
        print(f"{status} padFlash saturation: {saturation:.2f}")

    if failures:
        print("\nPalette audit failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(f"\nPalette audit passed for {args.factory}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
