---
name: create-new-theme
description: "Create, add, replace, or redesign a visual theme in the Loop Breaker JUCE audio plugin. Use when asked for a new color palette, a theme inspired by visual references, removal or replacement of an existing theme, theme-name migration, theme-specific typography or background effects, or updates to the theme selector and documentation. Covers ThemePalette roles, registration, saved-session compatibility, contrast and state visibility, regression tests, manual updates, and VST3 verification."
---

# Create New Theme

Create a cohesive Loop Breaker theme without rediscovering the theme system. Preserve the centralized palette architecture and validate both style and usability.

## Start Here

1. Inspect only the relevant working state:

   ```sh
   git status --short
   sed -n '1,380p' Source/ThemeEngine.cpp
   sed -n '670,820p' Source/ThemeEngine.cpp
   sed -n '1,170p' Source/ThemeEngineTests.cpp
   ```

2. Determine whether the user wants to add a theme or replace an existing one. Infer the visual direction from supplied references when possible; ask only if a missing choice would materially change the result.
3. Choose a concise display name. Keep parenthetical light/dark labels consistent with existing names when useful.
4. Build one strong visual concept rather than distributing unrelated bright colors across roles.

## Theme Architecture

Define built-in palettes as factory functions in `Source/ThemeEngine.cpp`. Register them in order inside `ThemeEngine::ThemeEngine()`. The Settings theme dropdown reads `getAvailableThemeNames()` automatically.

Use every `ThemePalette` role:

| Roles | Purpose |
| --- | --- |
| `bg`, `bgAlt` | Editor and tab-bar backgrounds |
| `panel`, `panelAlt` | Cards, controls, knob centers, and alternate surfaces |
| `border`, `borderGlow` | Structural outlines and luminous emphasis |
| `textPrimary`, `textSecondary`, `textOnAccent` | Main text, subdued text, and content placed on `accent1` |
| `accent1`, `accent2`, `accent3` | Primary interaction, alternate modifier color, and tertiary signal |
| `good`, `warn`, `bad` | Semantic success, caution, and error states |
| `knobFill`, `knobTrack` | Knob/slider active and inactive strokes |
| `waveformFill`, `playhead` | Loaded-pad oscilloscope content |
| `padEmpty`, `padLoaded` | Outer empty tile and loaded waveform aperture |
| `padSelected` | Translucent overlay; must brighten both pad surfaces |
| `padSelectedIndicator` | Selection dot and underline |
| `padFlash`, `padPlaying` | Modifier impact flash and active playback signal |
| `glowIntensity`, `borderRadius` | Theme-wide energy and geometry |

Prefer literal `hex()` / `hexA()` assignments for signature colors. Aliases such as `p.padPlaying = p.accent3` are fine.

## Implement the Palette

Use this complete shape:

```cpp
static ThemePalette makeYourTheme()
{
    ThemePalette p;
    p.name          = "Your Theme (Dark)";

    p.bg            = hex (0x000000);
    p.bgAlt         = hex (0x000000);
    p.panel         = hex (0x000000);
    p.panelAlt      = hex (0x000000);
    p.border        = hex (0x000000);
    p.borderGlow    = hex (0x000000);
    p.textPrimary   = hex (0xFFFFFF);
    p.textSecondary = hex (0xB0B0B0);
    p.textOnAccent  = hex (0x000000);
    p.accent1       = hex (0x00FFFF);
    p.accent2       = hex (0xFF00FF);
    p.accent3       = hex (0xFFFF00);
    p.good          = hex (0x00FF80);
    p.warn          = hex (0xFFD000);
    p.bad           = hex (0xFF3050);
    p.knobFill      = p.accent1;
    p.knobTrack     = hex (0x303030);
    p.waveformFill  = p.accent3;
    p.playhead      = p.accent2;
    p.padEmpty      = hex (0x101010);
    p.padLoaded     = hex (0x020202);
    p.padSelected   = hexA (0xFFFFFF, 0.22f);
    p.padSelectedIndicator = p.accent1;
    p.padFlash      = p.accent2;
    p.padPlaying    = p.accent3;
    p.glowIntensity = 0.8f;
    p.borderRadius  = 4.0f;
    return p;
}
```

Register it with `builtInPalettes.push_back (makeYourTheme());`.

### When Replacing a Theme

- Replace its factory and constructor entry so the retired name disappears from the selector.
- For this pre-release project, map the retired display name to `"Control Surface"` in `canonicalThemeName()` unless the user explicitly requests a different migration.
- Keep state restoration canonical: `PluginProcessor.cpp` should resolve a saved name with `getBuiltInPalette()` and store `restoredPalette->name`.
- Add regression coverage showing that the old name resolves to Control Surface.

Do not change the `SessionSettings` default unless the user explicitly asks to change the product default.

## Visual and Accessibility Rules

- Keep primary and secondary text at least 4.5:1 against the surfaces where they appear.
- Keep `textOnAccent` at least 4.5:1 against `accent1`.
- Keep active accents, borders, waveforms, and playheads at least 3:1 against their immediate surfaces.
- Make `padSelected` brighten both `padEmpty` and `padLoaded`; the existing unit test enforces this.
- Give `padFlash` strong saturation and clear separation from `padPlaying`.
- Use geometry as part of the concept: low radius feels technical/brutalist; high radius feels playful/soft.
- Use `glowIntensity` deliberately. Reserve values near `1.0f` for themes intended to feel luminous.
- Avoid a bright main background paired with light global text, or a dark panel paired with dark global text. Text roles are reused broadly.

Run the bundled audit after adding the factory:

```sh
python3 .github/skills/create-new-theme/scripts/check_theme_palette.py \
  Source/ThemeEngine.cpp makeYourTheme
```

Fix failures before building. Treat warnings as prompts for visual judgment.

## Optional Theme-Specific Behavior

Only add special behavior when the concept requires more than colors:

- `Source/ThemeFonts.h`: exact-name font selection. Default themes already use Rajdhani plus JetBrains Mono.
- `Source/BackgroundAnimator.h`: background motion and exact-name effects. Do not make expensive animation the default.
- `Source/ThemeLookAndFeel.h`: global component rendering. Avoid theme-name branches here unless a palette role cannot express the design.

Prefer palette-driven behavior so future themes remain cheap to add.

## Update Tests and Documentation

In `Source/ThemeEngineTests.cpp`:

1. Assert the new name is available and a retired name is absent when replacing.
2. Assert legacy aliases resolve to the canonical palette.
3. Lock two or three signature colors plus `glowIntensity` and `borderRadius`.
4. Update the expected `padFlash` table.
5. Preserve the all-theme selected-pad brightness test.

In `generate_manual.py`, update both:

- The Settings-tab `themes` list around the “Available themes” text.
- The `theme_descs` list in the Themes chapter.

Keep the documented theme count accurate. Do not generate or commit the PDF unless requested.

## Verify

Run:

```sh
git diff --check
python3 -m py_compile generate_manual.py
cmake --build build --target LoopBreakerTests -j 4
ctest --test-dir build --output-on-failure
cmake --build build --target LoopBreaker_VST3 -j 4
```

The build target regenerates `Source/BuildInfo.h`. If its timestamp was clean before the build, restore only that generated timestamp so the final diff contains intentional changes.

Finish by checking:

```sh
git status --short
rg -n 'Old Theme Name|New Theme Name' \
  Source/ThemeEngine.cpp Source/ThemeEngineTests.cpp Source/PluginProcessor.cpp generate_manual.py
```

Report the theme concept, migration behavior, and verification results.
