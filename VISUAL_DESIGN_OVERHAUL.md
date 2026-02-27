# Visual Design Overhaul — BufferTest Plugin

> **Status:** In Progress — Phase 1 Complete  
> **Date:** 2025-02-27  
> **Updated:** 2026-02-27  
> **Goal:** Transform the UI from a utilitarian developer tool into a visually immersive instrument that feels at home in an experimental electronic music workflow.

---

## 1. Design Philosophy

The current UI is a clean, light-mode, flat layout — functional but clinical. It feels like an engineering test harness, not a creative instrument. The redesign targets these pillars:

| Pillar                    | Description                                                                                                                                                                                                   |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Rave-ready atmosphere** | Dark base with luminous, neon-tinted accents. Background colors shift over time like ambient VJ material — toggleable for users who prefer stability.                                                         |
| **Light skeuomorphism**   | Knobs, pads, and panels have subtle depth cues (inner shadows, soft glows, slight gradients) without going full-photorealistic. Think beveled edges on pads, not wooden console textures.                     |
| **Retro-digital hybrid**  | Vaguely 8-bit/pixel-art influence in select areas (modifier badges, status readouts, waveform grid overlay) fused with modern rounded geometry. Evokes the "video game queue" concept at the core of the app. |
| **Motion as feedback**    | Subtle, purposeful animation — pad glow pulses, progress bar shimmer, background color drift — that communicate state and also make the UI feel alive. All animation is toggleable.                           |
| **Multi-theme support**   | Ship with 3–5 built-in themes; users pick one from a dropdown. Each theme defines a full color palette, glow intensity, animation speed, and optional background motion.                                      |

### Inspiration Reference

| Source                        | What to borrow                                                                                                                                                |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **ValhallaFutureVerb**        | Dark background, green/yellow neon glow on controls, large knobs with concentric rings, panel grouping with thin glowing borders                              |
| **ValhallaVintageVerb**       | Warm-toned dark palette, multi-ring decay knob, color-coded sections (orange/yellow/red gradient), serif-style title typography                               |
| **Koala Sampler**             | Pad grid with colored waveform thumbnails, pink/blue accent color coding for selection vs. empty, playful but functional pad layout, mobile-grade compactness |
| **Baby Audio Transit**        | Cream/light option with yellow/green accents, modular card layout with individual FX blocks, clean knob rings with indicator dots, bottom mix/output bars     |
| **VJ kaleidoscope aesthetic** | Slow-cycling hue background, radial symmetry motifs used as subtle watermarks behind content, cyan/teal bioluminescent glow                                   |

---

## 2. Theme System Architecture

### 2.1 Replace static `Theme.h` with a dynamic `ThemeEngine`

The current `Theme.h` is a set of `inline` functions returning hardcoded `juce::Colour` values. Replace it with a runtime-switchable theme engine.

```
ThemeEngine (singleton / shared ref)
├── currentTheme: ThemePalette  (struct of ~20 named colors)
├── animationSettings: AnimationConfig
├── backgroundMode: enum { Static, SlowCycle, Reactive }
├── setTheme(ThemePalette)
├── getColor(ColorRole) → juce::Colour  (interpolated if transitioning)
└── listeners: vector<ThemeListener*>   (repaint on theme change)
```

#### `ThemePalette` struct

```cpp
struct ThemePalette
{
    juce::String name;           // "Neon Rave", "Vintage Ember", etc.

    // Backgrounds
    juce::Colour bg;             // Main app background
    juce::Colour bgAlt;          // Alternate background for depth layers
    juce::Colour panel;          // Card/panel fill
    juce::Colour panelAlt;       // Subtle alternate panel

    // Borders
    juce::Colour border;         // Default border
    juce::Colour borderGlow;     // Accent-tinted border for active elements

    // Text
    juce::Colour textPrimary;
    juce::Colour textSecondary;
    juce::Colour textOnAccent;   // Text on accent-colored backgrounds

    // Accents
    juce::Colour accent1;        // Primary interactive color
    juce::Colour accent2;        // Secondary interactive color
    juce::Colour accent3;        // Tertiary / highlight

    // Semantic
    juce::Colour good;
    juce::Colour warn;
    juce::Colour bad;

    // Special
    juce::Colour knobFill;       // Rotary fill arc
    juce::Colour knobTrack;      // Rotary background track
    juce::Colour waveformFill;   // Waveform color
    juce::Colour playhead;       // Playhead line
    juce::Colour padEmpty;       // Empty pad background
    juce::Colour padLoaded;      // Pad with a loaded sample
    juce::Colour padSelected;    // Selected pad overlay
    juce::Colour padPlaying;     // Actively playing pad glow

    // Animation / glow
    float glowIntensity;         // 0.0 = no glow, 1.0 = full neon
    float borderRadius;          // Default corner radius for cards/pads
};
```

#### `AnimationConfig` struct

```cpp
struct AnimationConfig
{
    bool enabled = true;                  // Master toggle
    bool backgroundColorCycle = true;     // Slow hue rotation on bg
    bool padPulseOnTrigger = true;        // Pads glow-pulse when modifiers fire
    bool progressBarShimmer = true;       // Subtle shimmer on countdown bar
    bool knobGlowOnChange = true;         // Knobs emit soft glow when being adjusted
    float animationSpeed = 1.0f;          // Global speed multiplier (0.25x – 2.0x)
    float backgroundCycleRate = 0.02f;    // Hue degrees per frame (~0.4°/sec at 20Hz)
};
```

### 2.2 Built-in Themes

#### Theme 1: **"Neon Rave"** (Default)

The flagship dark theme. Inspired by VJ kaleidoscope imagery and FutureVerb.

| Role          | Color                           | Hex                   |
| ------------- | ------------------------------- | --------------------- |
| bg            | Near-black with blue-green tint | `#0A0E14`             |
| bgAlt         | Slightly lighter                | `#111822`             |
| panel         | Dark charcoal                   | `#161E2A`             |
| panelAlt      | Muted dark                      | `#1A2332`             |
| border        | Deep slate                      | `#2A3545`             |
| borderGlow    | Cyan glow                       | `#00E5FF`             |
| textPrimary   | Off-white                       | `#E8ECF0`             |
| textSecondary | Cool grey                       | `#7A8A9E`             |
| textOnAccent  | Black                           | `#0A0E14`             |
| accent1       | Electric cyan                   | `#00E5FF`             |
| accent2       | Neon magenta                    | `#FF00E5`             |
| accent3       | Hot green                       | `#39FF14`             |
| good          | Green                           | `#39FF14`             |
| warn          | Amber                           | `#FFB800`             |
| bad           | Hot red                         | `#FF2D55`             |
| knobFill      | Cyan→Magenta gradient endpoints | `#00E5FF` → `#FF00E5` |
| waveformFill  | Teal                            | `#00BFA6`             |
| playhead      | Bright cyan                     | `#00E5FF`             |
| padEmpty      | `#111822` with 40% glow border  |                       |
| padLoaded     | `#1A2332` with waveform         |                       |
| padSelected   | Cyan 15% overlay                |                       |
| padPlaying    | Pulsing green glow outline      |                       |
| glowIntensity | 0.85                            |                       |
| borderRadius  | 6.0                             |                       |

**Background animation:** Very slow hue-rotation on `bg` — shifts from blue-tinted black → teal-tinted → purple-tinted → back. Cycle period ~60 seconds. Feels like a rave ambient wash.

#### Theme 2: **"Vintage Ember"**

Warm dark theme. Inspired by ValhallaVintageVerb and analog gear.

| Role          | Color            | Hex       |
| ------------- | ---------------- | --------- |
| bg            | Deep brown-black | `#1A0F0A` |
| bgAlt         | Warm dark        | `#241610` |
| panel         | Dark sienna      | `#2D1C14` |
| panelAlt      | Brown-charcoal   | `#352218` |
| border        | Muted copper     | `#4A3528` |
| borderGlow    | Amber            | `#FF8C00` |
| textPrimary   | Warm cream       | `#F5E6D0` |
| textSecondary | Dusty rose       | `#A08070` |
| accent1       | Burnt orange     | `#FF6B2B` |
| accent2       | Gold             | `#FFAA00` |
| accent3       | Deep red         | `#E03030` |
| good          | Warm green       | `#7ACC3D` |
| warn          | Gold             | `#FFAA00` |
| bad           | Crimson          | `#E03030` |
| waveformFill  | Orange           | `#FF8C42` |
| playhead      | Gold             | `#FFAA00` |
| glowIntensity | 0.6              |           |
| borderRadius  | 5.0              |           |

**Background animation:** Slow ember glow — subtle orange↔red brightness oscillation in the `bg` color. Feels like sitting by hot coils.

#### Theme 3: **"Pixel Grid"**

The 8-bit-inspired theme. High contrast, limited palette, pixel-adjacent geometry.

| Role          | Color                      | Hex       |
| ------------- | -------------------------- | --------- |
| bg            | True black                 | `#000000` |
| bgAlt         | Dark grey                  | `#111111` |
| panel         | Dark charcoal              | `#1A1A1A` |
| panelAlt      | Off-black                  | `#222222` |
| border        | Bright grid lines          | `#333333` |
| borderGlow    | Pixel green (CRT phosphor) | `#33FF33` |
| textPrimary   | CRT green                  | `#33FF33` |
| textSecondary | Dim green                  | `#1A8A1A` |
| accent1       | Pixel green                | `#33FF33` |
| accent2       | Pixel cyan                 | `#33FFFF` |
| accent3       | Pixel magenta              | `#FF33FF` |
| good          | Green                      | `#33FF33` |
| warn          | Yellow                     | `#FFFF33` |
| bad           | Red                        | `#FF3333` |
| waveformFill  | Green                      | `#33FF33` |
| playhead      | Cyan                       | `#33FFFF` |
| glowIntensity | 0.9                        |           |
| borderRadius  | 2.0                        |           |

**Background animation:** Scanline sweep — a subtle horizontal bright line scrolling down the window (like a CRT refresh), very faint. Optional static noise texture.

**Typography:** Use a monospaced/pixel-style font for value readouts and modifier names. Pad labels use small-caps pixel font. Section headers get a slight 1px drop shadow.

#### Theme 4: **"Ultraviolet"**

Cool, club-lighting purple/blue palette.

| Role          | Color          | Hex       |
| ------------- | -------------- | --------- |
| bg            | Deep indigo    | `#0C0618` |
| bgAlt         | Darker purple  | `#120B22` |
| panel         | Dark violet    | `#1A1030` |
| panelAlt      | Purple-grey    | `#221640` |
| border        | Muted lavender | `#3D2A60` |
| borderGlow    | UV purple      | `#BF40FF` |
| textPrimary   | Lavender white | `#E8DEFF` |
| textSecondary | Muted lilac    | `#8A70B0` |
| accent1       | UV purple      | `#BF40FF` |
| accent2       | Electric blue  | `#4D7CFF` |
| accent3       | Pink           | `#FF40A0` |
| good          | Mint           | `#40FFA0` |
| warn          | Gold           | `#FFD040` |
| bad           | Hot pink       | `#FF4080` |
| waveformFill  | Blue-purple    | `#7C5CFF` |
| playhead      | UV purple      | `#BF40FF` |
| glowIntensity | 0.8            |           |
| borderRadius  | 6.0            |           |

**Background animation:** Slow purple↔blue oscillation with optional subtle kaleidoscope-like radial symmetry pattern at very low opacity behind the content.

#### Theme 5: **"Studio Clean"**

For users who want minimal visual distraction. Refined dark mode, no animation by default.

| Role          | Color            | Hex       |
| ------------- | ---------------- | --------- |
| bg            | Dark grey        | `#1E1E1E` |
| bgAlt         | Slightly lighter | `#252525` |
| panel         | Medium-dark      | `#2D2D2D` |
| panelAlt      | Lighter grey     | `#333333` |
| border        | Medium grey      | `#444444` |
| borderGlow    | White            | `#FFFFFF` |
| textPrimary   | White            | `#EBEBEB` |
| textSecondary | Grey             | `#999999` |
| accent1       | Soft blue        | `#5B9BD5` |
| accent2       | Teal             | `#4EC9B0` |
| accent3       | Peach            | `#D4845E` |
| good          | Green            | `#6CC644` |
| warn          | Yellow           | `#E5C07B` |
| bad           | Red              | `#E06C75` |
| waveformFill  | Soft blue        | `#5B9BD5` |
| playhead      | White            | `#EBEBEB` |
| glowIntensity | 0.0              |           |
| borderRadius  | 4.0              |           |

**Background animation:** None by default. Can be enabled — very subtle brightness breathing.

---

## 3. Component-by-Component Redesign

### 3.1 Pad Grid (PadGridComponent)

The pad grid is the visual centerpiece. It should convey energy and state at a glance.

**Shape & Layout:**

- Keep the 2×4 grid layout (bottom row = pads 1–4, top = 5–8).
- Increase pad corner radius to match theme's `borderRadius`.
- Pads get a subtle inner shadow (dark vignette around edges) for depth.
- 2px gap between pads → 3px gap for breathing room, with the gap itself faintly glowing in the theme's `borderGlow` color at very low opacity.

**Pad States — Visual Language:**

| State                          | Treatment                                                                                                                                                                                        |
| ------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Empty**                      | `padEmpty` fill, dashed border (2px dash, 4px gap) in `border` color, centered "+" icon (or "DROP" text in pixel font for Pixel Grid theme). No waveform.                                        |
| **Loaded (idle)**              | `padLoaded` fill, solid `border` outline. Waveform rendered in `waveformFill` color with 60% opacity. Filename label below.                                                                      |
| **Selected**                   | `padSelected` color overlay (15% alpha tint). Border becomes `accent1` at full glow intensity. Slight outer glow (box-shadow effect via multiple concentric transparent rects).                  |
| **Playing**                    | Pulsing `padPlaying` glow outline — 2px wide, oscillating between 60% and 100% alpha at ~2Hz. If animation disabled: solid `padPlaying` border at 85% alpha (current behavior, recolored).       |
| **Modifier triggered (flash)** | Brief burst of `accent2` or `accent3` glow blending outward from center. Decays over ~400ms. If animation disabled: solid tint overlay for 300ms.                                                |
| **MIDI Learn**                 | Animated marching-ants dashed border (border segments rotate around the pad perimeter). "LEARN" badge floats center with `warn` background. If animation disabled: static dashed border + badge. |
| **File drag hover**            | Pad fill brightens by 15%. Border becomes `accent1` dashed. "Drop to load" text fades in. Multi-file hint shows count badge.                                                                     |

**Waveform Rendering:**

- Waveform filled with a vertical gradient from `waveformFill` (top) to `waveformFill.darker(0.3)` (bottom) for depth.
- Thin `border` colored grid lines behind the waveform (4 horizontal divisions) — evokes an oscilloscope look.
- Playhead: 2px line in `playhead` color with a small triangle head at the top, and a faint vertical glow (blur) behind it.
- Loop region: semi-transparent `warn` fill (current behavior) but with subtle diagonal hatching (45° lines, 4px apart) for texture.

**Pad Number Badges:**

- Each pad displays its number (1–8) in the top-left corner as a small rounded-rect badge.
- Badge background: `accent1` at 30% alpha. Text: `textPrimary` in bold 10px.
- MIDI note badge moves to top-right (as current). Style: `panelAlt` background, `textSecondary` text, small rounded rect.

### 3.2 Knobs & Sliders

**Rotary Knobs** (Master Volume, any future knobs):

- Outer ring: `knobTrack` color, 3px wide.
- Fill arc: `knobFill` color (or gradient from `accent1` → `accent2` swept along the arc for Neon Rave theme).
- Indicator dot: small filled circle at the arc endpoint in `textPrimary`, replaces the default pointer.
- Center fill: radial gradient from `panel` (center) to `panelAlt` (edge) — creates the soft "beveled" look.
- Light skeuomorphic touch: a tiny specular highlight (1px white arc at ~10 o'clock) suggests a light source. Very subtle.
- On value change (if animation enabled): brief outer glow pulse in `accent1` that fades over 200ms.
- Value text below: monospaced font, `textSecondary`.

**Linear Sliders** (Bars/Mod, probability sliders):

- Track: rounded rect in `knobTrack` / `panelAlt`, 6px tall.
- Fill: `accent1` color (or gradient), rounded on the left end.
- Thumb: small circle (10px diameter) in `accent1` with a 1px `borderGlow` ring.
- Probability sliders: fill color can be tinted by modifier category (time-based = cyan, pitch = magenta, FX = green, etc.) for visual grouping.

### 3.3 Upcoming Modifier Display

This is the "heads-up" tactical readout — should feel like a game HUD element.

- Background: `panel` fill with thin `border` divider from the rest of the UI.
- Next modifier name: larger font (18–20px), bold, in `accent1` color. If modifier affects a specific domain (time, pitch, FX), tint the text to match that domain's color.
- Variant/detail: smaller font (12px) in `textSecondary`.
- **Countdown progress bar:**
  - Track: `panelAlt` rounded rect.
  - Fill: gradient from `accent2` (left) to `accent1` (right), growing rightward as the countdown progresses.
  - If animation enabled: subtle animated shimmer — a bright highlight sweeps across the fill bar from left to right every ~2 seconds.
  - Final 25%: fill color transitions to `warn` (urgency indication).
- "PAUSED" overlay: `warn` text with pulsing opacity (if animation enabled).
- Bar/beat counter: monospaced digits in `textSecondary`.

### 3.4 Tab Bar & Navigation

- Replace JUCE's default `TabbedComponent` tab bar styling.
- Tab bar background: `bgAlt`.
- Inactive tab: `textSecondary` text, no background.
- Active tab: `accent1` text, thin `accent1` bottom-border (2px underline), no full background fill. Clean and minimal.
- Hover: text brightens to `textPrimary`.
- Tab labels: "SESSION", "PROBABILITY", "SETTINGS", "DEBUG", "HELP" — all-caps, 11px, letter-spaced +1px.

### 3.5 Modifier Probability Panel (Modifiers Tab)

- Category headers get a thin colored left-border accent (like a sidebar indicator) in the category's tint color.
- Modifier name labels: `textSecondary`, but when hovered → `textPrimary`.
- Value labels: when at 0% (OFF), text is `bad` color. When at 100%, text is `good`. In between, text is `textSecondary`.
- "Reset All to 100%" button: pill-shaped, `accent1` fill, `textOnAccent` text. On hover: glow effect.

### 3.6 Modifier History Panel (Debug Tab)

- Maintain the color-coded modifier entries but use the theme's accent colors.
- Timestamps: monospaced, `textSecondary`.
- Modifier names: bold, colored by category.
- Target pads: displayed as small colored pad-number badges inline.
- Scrollbar: thin, `border` track, `textSecondary` thumb.

### 3.7 Help Panel

- Section headings: `accent1`, bold, with a horizontal `border` divider below.
- Key badges: `panelAlt` rounded rects with `borderGlow` tint at 20%.
- Body text: `textSecondary`, 13.5px, generous line height (1.5×).
- Keyboard shortcut text: monospaced, `accent2`.

### 3.8 Background Animation System

The background is the most distinctive visual feature — it turns the plugin window into a living, breathing surface.

**Implementation: `BackgroundAnimator` component**

Sits behind all content as the bottom-most child of the editor. Paints to an offscreen `juce::Image` and composites.

**Modes:**

1. **Static** — Solid `bg` color. No animation. Zero CPU overhead.

2. **Slow Cycle** — The `bg` color's hue rotates continuously. Lightness and saturation stay constant (or oscillate very gently). The cycle rate is configurable (default ~1° hue per second → full cycle in 6 minutes). This creates a kaleidoscopic ambient wash without being distracting.

3. **Reactive** — Background hue/brightness reacts to modifier triggers. When a modifier fires, the background briefly shifts toward that modifier category's accent color, then relaxes back. Creates a "the app is alive" feeling.

**Performance considerations:**

- Only repaint the background layer when a new frame is needed (timer-driven, 10–15 FPS max for background — decoupled from the 20Hz UI refresh).
- Use `juce::Image` as a cached background; only regenerate when the color actually changes.
- Provide a CPU-budget slider in settings: "Animation Quality" (Low / Medium / High) controlling FPS and effect complexity.

**Optional overlay effects** (only in "High" quality mode):

- Very faint radial gradient emanating from the center (like a spotlight) that slowly rotates.
- A subtle noise/grain texture (pre-generated static image) composited at 3–5% opacity for analog feel.
- Scanline effect (Pixel Grid theme only): thin horizontal lines at 2px intervals, 5% opacity.

---

## 4. Animation System

### 4.1 `Animator` Utility Class

A lightweight animation helper that components can use:

```cpp
class Animator
{
public:
    // Start an animation from 0→1 over durationMs
    void start(int durationMs, std::function<void(float progress)> onFrame,
               std::function<void()> onComplete = {});

    // Easing options
    enum Easing { Linear, EaseOut, EaseInOut, Bounce };

    // Called by a shared timer or the component's own timer
    void tick();

    bool isRunning() const;
};
```

Used for:

- Pad flash/glow pulse decay (400ms EaseOut)
- Knob glow pulse (200ms EaseOut)
- Progress bar shimmer (continuous loop)
- MIDI learn marching ants (continuous loop)
- Background color transitions (1000ms EaseInOut when switching themes)
- Tab underline slide (150ms EaseOut when switching tabs)

### 4.2 Animation Toggle

- Global toggle in `AnimationConfig::enabled` — disables ALL animation when off.
- Per-feature toggles for granular control (background cycle, pad pulse, shimmer, knob glow).
- When animation is disabled, all states snap to their final visual (no transition, just the end-state color/opacity).
- Setting persisted in `SessionSettings` and saved with the project.

---

## 5. Typography

### 5.1 Font Strategy

| Context                 | Font                                                                  | Style   | Size    |
| ----------------------- | --------------------------------------------------------------------- | ------- | ------- |
| Plugin title / branding | Custom display font (bundled)                                         | Bold    | 22px    |
| Section headings        | System sans-serif                                                     | Bold    | 15–17px |
| Control labels          | System sans-serif                                                     | Regular | 11–12px |
| Value readouts          | Monospaced (bundled, e.g. JetBrains Mono, Fira Code, or a pixel font) | Regular | 11–13px |
| Pad labels (filenames)  | System sans-serif                                                     | Regular | 11px    |
| Modifier names (HUD)    | System sans-serif or monospaced                                       | Bold    | 14–18px |
| Help body text          | System sans-serif                                                     | Regular | 13.5px  |

### 5.2 Bundled Fonts

Include 2 fonts as binary resources:

1. A **display/header font** with character — something slightly stylized but readable (e.g., Orbitron, Oxanium, or Rajdhani — all free Google Fonts with permissive licenses).
2. A **monospaced code/value font** — JetBrains Mono or Fira Code (OFL licensed).

For the **Pixel Grid** theme, optionally swap in a pixel font for value readouts (e.g., Press Start 2P, Silkscreen — also free).

---

## 6. Custom LookAndFeel

### 6.1 `ThemeLookAndFeel` — Replaces `HipLookAndFeel`

A single `LookAndFeel_V4` subclass that dynamically reads from `ThemeEngine` for all color values. Override methods:

| Override                      | Customization                                                               |
| ----------------------------- | --------------------------------------------------------------------------- |
| `drawRotarySlider()`          | Arc + dot indicator + center gradient + optional glow ring                  |
| `drawLinearSlider()`          | Rounded track + gradient fill + circle thumb                                |
| `drawToggleButton()`          | Rounded pill toggle (on/off) with accent color fill, smooth slide animation |
| `drawComboBox()`              | Rounded rect, themed colors, chevron arrow                                  |
| `drawTabButton()`             | Minimal underline style, no full background                                 |
| `drawButtonBackground()`      | Pill-shaped or rounded rect depending on context                            |
| `drawPopupMenuItem()`         | Themed hover highlight, category color dots                                 |
| `drawScrollbar()`             | Thin rounded thumb, transparent track                                       |
| `drawLabel()`                 | Themed text colors                                                          |
| `drawGroupComponentOutline()` | Thin left-accent-bar style instead of full box                              |

All drawing reads colors from `ThemeEngine::getColor(role)` at paint time — so theme switches take effect immediately.

---

## 7. Settings UI

### 7.1 Theme & Animation Settings Panel

Add a dedicated **Settings** tab to the tab bar (alongside Session, Modifiers, Debug, Help). This keeps appearance options easily discoverable without cluttering the Session tab:

```
┌─ Appearance ──────────────────────────────────┐
│                                                │
│  Theme:  [ Neon Rave ▾ ]                      │
│                                                │
│  ☑ Enable Animations                           │
│    ☑ Background color cycling                  │
│    ☑ Pad glow effects                          │
│    ☑ Progress bar shimmer                      │
│    ☐ Knob glow on change                       │
│                                                │
│  Animation Speed:  ──●─────  [1.0×]           │
│                                                │
│  Background Mode:  ( ) Static                  │
│                    (●) Slow Cycle               │
│                    ( ) Reactive                 │
│                                                │
└────────────────────────────────────────────────┘
```

### 7.2 Persistence

All visual settings are stored in `SessionSettings` and serialized with project state:

```cpp
// In SessionSettings
juce::String themeName = "Neon Rave";
bool animationsEnabled = true;
bool bgCycleEnabled = true;
bool padPulseEnabled = true;
bool progressShimmerEnabled = true;
bool knobGlowEnabled = false;
float animationSpeed = 1.0f;
int backgroundMode = 1; // 0=Static, 1=SlowCycle, 2=Reactive
```

---

## 8. Implementation Plan

### Phase 1: Foundation (Theme Engine + Base Palette)

1. ~~Create `ThemeEngine.h/cpp` with `ThemePalette`, `AnimationConfig`, `ColorRole` enum, and `getColor()`.~~ ✅
2. ~~Define all 5 built-in palettes as static `ThemePalette` instances.~~ ✅
3. ~~Retrofit all current `Theme::xxx()` call sites to use `ThemeEngine::getColor(ColorRole::xxx)`.~~ ✅ (backward-compatible `Theme::` namespace delegates to `ThemeEngine`)
4. ~~Create `ThemeLookAndFeel` class with all overrides reading from `ThemeEngine`.~~ ✅ (drawRotarySlider, drawLinearSlider, drawToggleButton, drawComboBox, drawButtonBackground, drawScrollbar)
5. ~~Add theme selection to `SessionSettings` and persist it.~~ ✅ (serialized in getStateInformation/setStateInformation)

### Phase 2: Dark Mode Conversion

6. ~~Invert the entire UI from the current light palette to dark (using "Neon Rave" as the new default).~~ ✅ (Neon Rave is now default; all Theme:: calls route through dark palette)
7. Audit all hard-coded colors in `PadGridComponent`, `UpcomingModifierDisplay`, `ModifierHistoryPanel`, `FxStatusPanel`, `TearingDebugPanel`, `HelpPanelContent`, `ModifierProbabilityPanel`, and `ModifierSelectionPanel`.
8. ~~Replace all `juce::Colours::black` / `juce::Colours::white` literals with theme roles.~~ ✅ (`ModifierSelectionPanel` migrated; `HelpPanelContent` badge border & body text contrast fixed)
9. Test all 5 themes for readability and contrast.

### Phase 3: Component Visual Refinements

10. Redesign `PadGridComponent::paintOverChildren()` — implement new pad states (inner shadow, glow borders, waveform gradient, oscilloscope grid lines, pad number badges, "+" empty icon).
11. Redesign rotary knob rendering (`drawRotarySlider()` override) — arc, dot, center gradient, specular highlight.
12. Redesign linear sliders and toggle buttons.
13. Redesign tab bar (underline style, all-caps labels).
14. Redesign `UpcomingModifierDisplay` — HUD style, gradient progress bar, domain-tinted modifier name.

### Phase 4: Animation

15. Implement `BackgroundAnimator` component with Static / Slow Cycle / Reactive modes.
16. Implement pad glow-pulse animation (on modifier trigger).
17. Implement progress bar shimmer.
18. Implement MIDI learn marching-ants animation.
19. Implement theme transition animation (crossfade colors over 500ms when switching themes).
20. Add animation toggle UI and wire to `AnimationConfig`.

### Phase 5: Typography & Polish

21. Bundle display font + monospaced font as binary resources.
22. Apply font assignments across all components.
23. Implement pixel-font variant for Pixel Grid theme.
24. Add subtle noise/grain texture overlay option.
25. Add scanline effect for Pixel Grid theme.

### Phase 6: Settings & QA

26. Build the Settings tab with Appearance section (theme dropdown, animation toggles, background mode).
27. Full pass on all 5 themes across all 5 tabs — verify contrast ratios, readability, visual coherence.
28. Performance profiling — ensure animation adds <2% CPU at 20Hz on a typical machine.
29. Test with DAW hosts (Logic, Ableton, Reaper) for rendering compatibility.
30. Verify plugin state save/restore preserves theme and animation settings.

---

## 9. Performance Budget

| Feature                           | Target         | Measurement                    |
| --------------------------------- | -------------- | ------------------------------ |
| Background animation (Slow Cycle) | < 0.5% CPU     | Profile with Instruments on M1 |
| Pad glow pulse (8 simultaneous)   | < 0.3% CPU     | Timer-driven, no re-layout     |
| Progress shimmer                  | < 0.1% CPU     | Single rect repaint            |
| Theme switch (color transition)   | < 50ms latency | Interpolate 20 colors          |
| Total UI thread overhead          | < 3% CPU       | Sum of all animation + repaint |

All animation is driven by `juce::Timer` callbacks (not `repaint()` floods). Each animated component only repaints its own bounds. The `BackgroundAnimator` uses a cached `juce::Image` to avoid redundant full-window repaints.

---

## 10. Accessibility Notes

- All themes must maintain a minimum contrast ratio of **4.5:1** for body text and **3:1** for large text (WCAG AA).
- The "Studio Clean" theme serves as the high-contrast, low-distraction option.
- Animation can be fully disabled with a single toggle.
- Color coding is always accompanied by text labels (no color-only information).
- The plugin respects the host's UI scaling factor for HiDPI displays.

---

## Appendix A: Color Role Reference

Complete list of `ColorRole` enum values and their usage across components:

```
Bg, BgAlt, Panel, PanelAlt,
Border, BorderGlow,
TextPrimary, TextSecondary, TextOnAccent,
Accent1, Accent2, Accent3,
Good, Warn, Bad,
KnobFill, KnobTrack,
WaveformFill, Playhead,
PadEmpty, PadLoaded, PadSelected, PadPlaying
```

Every `Theme::xxx()` call in the current codebase maps to one of these roles. The migration is a 1:1 replacement.

## Appendix B: File Inventory — Current Theme Usage

| File                         | Theme refs                                                                                                            | Notes                                    |
| ---------------------------- | --------------------------------------------------------------------------------------------------------------------- | ---------------------------------------- |
| `Theme.h`                    | Defines all colors                                                                                                    | **Replace** with `ThemeEngine.h`         |
| `PadGridComponent.h`         | `bg`, `panel`, `panelAlt`, `border`, `borderStrong`, `text`, `textSubtle`, `accent`, `accent2`, `good`, `warn`, `bad` | Heaviest user — full repaint overhaul    |
| `PluginEditor.cpp`           | `bg`, `panel`, `panelAlt`, `border`, `borderStrong`, `text`, `textSubtle`, `accent`, `warn`                           | `HipLookAndFeel` + `PluginEditorContent` |
| `UpcomingModifierDisplay.h`  | `panel`, `panelAlt`, `border`, `borderStrong`, `text`, `textSubtle`, `accent2`, `warn`                                | Custom paint                             |
| `ModifierHistoryPanel.h`     | `panel`, `panelAlt`, `border`, `text`, `textSubtle`, `accent`, `accent2`, `good`, `warn`                              | ListBox + custom row paint               |
| `ModifierProbabilityPanel.h` | `text`, `textSubtle`, `accent`, `panelAlt`                                                                            | Sliders + labels                         |
| `FxStatusPanel.h`            | `panel`, `border`, `text`, `textSubtle`, `bad`, `warn`                                                                | Custom paint                             |
| `TearingDebugPanel.h`        | `panel`, `border`, `text`, `textSubtle`, `bad`, `warn`                                                                | Custom paint                             |
| `HelpPanelContent.h`         | `panel`, `panelAlt`, `border`, `borderStrong`, `text`, `textSubtle`, `accent`                                         | Custom text layout                       |
| `ModifierSelectionPanel.h`   | `juce::Colours::black` (hardcoded)                                                                                    | Needs migration to theme                 |
| `DebugPanelContent.h`        | `bg`                                                                                                                  | Background fill                          |
