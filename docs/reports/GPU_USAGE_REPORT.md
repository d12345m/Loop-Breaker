# GPU Usage Investigation Report

**System:** MacBook Pro M3 Max (30 GPU cores)
**Observed GPU Usage:** ~30%
**Date:** April 14, 2026

---

## Executive Summary

The plugin drives ~30% GPU usage through a combination of **continuous full-screen repainting**, **multiple unsynchronized high-frequency timers**, **expensive per-frame geometry generation**, and **pervasive alpha-blended overlays** that defeat Metal compositor optimizations. No OpenGL context is used — all rendering goes through JUCE's CoreGraphics/Metal software path, meaning every `repaint()` call submits new GPU work through the macOS compositor.

---

## Critical Findings

### 1. BackgroundAnimator: Full-Screen Repaint at 15 Hz (Estimated ~10-15% GPU)

**File:** [Source/BackgroundAnimator.h](Source/BackgroundAnimator.h)

The `BackgroundAnimator` runs a **15 Hz timer** (line 41) that triggers a full-window repaint every 66ms. Each frame:

- **Regenerates a full-screen radial gradient** into a cached `juce::Image`. Despite the caching strategy, `cachedBgColour` is explicitly invalidated every tick (line 255: `cachedBgColour = juce::Colour()`) because the hue offset advances continuously. This means the gradient is recomputed into the image buffer every single frame.
- **Tiles a 256×256 noise texture** across the entire window at 3–5% opacity (lines 191–206). This is a nested loop of `drawImageAt()` calls — on a 1920×1080 window, that's ~32 tile draws per frame, each requiring alpha compositing.
- **Draws 200+ scanline strokes** (Pixel Grid theme only, lines 208–225) at 5% opacity with a sweeping glow line.

**Why it's expensive:** The cached image approach helps avoid allocation, but the gradient is still regenerated into it every frame. The noise tiling and scanlines are drawn directly to the Graphics context on top, requiring pixel-by-pixel alpha blending across the full window area at 15 FPS.

---

### 2. Five Concurrent 15 Hz Timers (Estimated ~5-7% GPU overhead)

Five independent `Timer` instances all fire at 15 Hz with no synchronization:

| Component               | File                                                          | Line | Timer Hz |
| ----------------------- | ------------------------------------------------------------- | ---- | -------- |
| BackgroundAnimator      | [BackgroundAnimator.h](Source/BackgroundAnimator.h)           | 41   | 15       |
| PadGridComponent        | [PadGridComponent.h](Source/PadGridComponent.h)               | 75   | 15       |
| PluginEditorContent     | [PluginEditor.cpp](Source/PluginEditor.cpp)                   | 143  | 15       |
| UpcomingModifierDisplay | [UpcomingModifierDisplay.h](Source/UpcomingModifierDisplay.h) | 65   | 15       |
| PresetBarComponent      | [PresetBarComponent.h](Source/PresetBarComponent.h)           | 73   | 15       |

Additionally:

| ThemeLookAndFeel (knob glow) | [ThemeLookAndFeel.h](Source/ThemeLookAndFeel.h) | ~200 | 30 |
| ThemeEngine (crossfade) | [ThemeEngine.cpp](Source/ThemeEngine.cpp) | 737 | 30 (transient) |
| FxStatusPanel | [FxStatusPanel.h](Source/FxStatusPanel.h) | 15 | 5 |

**Impact:** When timers fire independently, their repaints cannot be coalesced by the JUCE message thread. Five 15 Hz timers that fire at slightly different offsets produce up to **75 repaint messages per second** instead of a consolidated 15. Each repaint message results in a separate GPU composite pass.

---

### 3. PadGridComponent: Highest Paint Complexity

**File:** [Source/PadGridComponent.h](Source/PadGridComponent.h)

Each call to `paintOverChildren()` renders **per pad** (up to 8 pads):

- Colored background fill with inner shadow vignette (radial gradient)
- Waveform drawing via AudioThumbnail with vertical gradient
- Loop region: semi-transparent fill + diagonal cross-hatching (45° lines at 4px spacing)
- Playhead: 2px line + glow behind it (alpha 0.12) + triangle head
- Selection overlay: semi-transparent fill + glow border + outer concentric glow ring
- MIDI learn: marching-ants dashed border (generated per frame) + "LEARN" badge
- Playing state: glow outline border
- Flash overlay: solid tint + radial gradient glow

**Multiplied by 8 pads, this is the single most expensive paint routine in the entire plugin.** The combination of gradients, alpha overlays, cross-hatching paths, and dashed strokes is extremely costly.

The PadGridComponent has a conditional repaint gate: it only repaints when
flash counters are active, glow animators are running, or MIDI learn is active.

---

### 4. Dashed Stroke Generation Per Frame

**Files:** [PresetBarComponent.h](Source/PresetBarComponent.h) (line 130), [PadGridComponent.h](Source/PadGridComponent.h), [PluginEditor.cpp](Source/PluginEditor.cpp)

`PathStrokeType::createDashedStroke()` constructs new path geometry from scratch every paint call. For marching-ants animations, this runs **every frame** for every slot/pad in MIDI learn mode:

```cpp
juce::PathStrokeType strokeType(2.0f);
juce::Path dashed;
strokeType.createDashedStroke(dashed, outline, dashLengths, 2);
```

This is a CPU-intensive path tessellation operation that generates dozens of sub-paths from the original rounded rectangle. It should be cached and offset rather than recreated.

---

### 5. ThemeLookAndFeel Knob Glow at 30 Hz

**File:** [ThemeLookAndFeel.h](Source/ThemeLookAndFeel.h) (line ~200)

When any rotary slider value changes, the LookAndFeel starts a **30 Hz timer** for glow decay animation. The timer is shared across all sliders but:

- Each slider with a non-zero `glowAlpha` draws **two concentric alpha-blended ellipses** (50% and 20% opacity)
- The timer is only stopped when all glow states have decayed to zero, but there's no explicit check — it relies on the timer's own lifecycle
- Multiple sliders being adjusted simultaneously accumulate glow rendering cost

---

### 6. Alpha-Blended Overlays Prevent Compositor Optimization

Semi-transparent rendering is pervasive throughout the plugin:

| Effect                   | Opacity  | Location            |
| ------------------------ | -------- | ------------------- |
| Noise texture overlay    | 3–5%     | BackgroundAnimator  |
| Scanline lines           | 5%       | BackgroundAnimator  |
| Pad selection overlay    | Variable | PadGridComponent    |
| Playhead glow            | 12%      | PadGridComponent    |
| Cross-hatch loop overlay | Variable | PadGridComponent    |
| Preset slot backgrounds  | 15–50%   | PresetBarComponent  |
| Divider lines            | 35–50%   | Multiple components |
| Scrollbar track          | 10%      | ThemeLookAndFeel    |
| PopupMenu highlight      | 12%      | ThemeLookAndFeel    |

**Why this matters on M3 Max:** macOS Metal compositor can skip compositing for opaque layers but must perform pixel-by-pixel blending for any layer with transparency. When nearly every component uses alpha, the compositor cannot optimize away any passes.

---

## Optimization Recommendations

### Tier 1: High Impact, Low Risk

#### 1A. Add `isShowing()` Guard to BackgroundAnimator Timer

The timer callback already has this guard (line 240), but verify it's working in all DAW hosts. Some DAWs keep the editor "showing" even when the window is behind another.

**Estimated savings:** 10-15% when editor is hidden

#### 1B. Consolidate Timers into a Single 15 Hz Master Timer

Replace the 5 independent 15 Hz timers with a single timer that dispatches to subscribers. This ensures all repaints are coalesced into one compositor pass per frame.

```cpp
// Single timer drives all UI updates
void timerCallback() override {
    backgroundAnimator.onTick(dt);
    padGrid.onTick(dt);
    upcomingDisplay.onTick(dt);
    presetBar.onTick(dt);
    // Single consolidated repaint
}
```

**Estimated savings:** 3-5% GPU

#### 1C. Reduce BackgroundAnimator to 5 Hz or Lower

A slowly cycling hue background does not need 15 FPS. At 5 Hz (200ms) with eased interpolation, the visual difference is imperceptible for a background gradient.

**Estimated savings:** 6-10% GPU

---

### Tier 2: Medium Impact, Medium Effort

#### 2A. Cache Dashed Stroke Geometry

Pre-compute the dashed path once and reuse it, applying a transform offset for the marching-ants animation instead of regenerating:

```cpp
// Generate once, then translate for animation
if (cachedDashPath.isEmpty() || boundsChanged)
    strokeType.createDashedStroke(cachedDashPath, outline, dashLengths, 2);
g.strokePath(cachedDashPath, strokeType,
             AffineTransform::translation(dashOffset, 0));
```

**Estimated savings:** 2-3% GPU during MIDI learn

#### 2B. Reduce Noise Texture to Single Pre-Composited Image

Instead of tiling a 256×256 texture across the window every frame, pre-render a full-window noise image when the window resizes, and draw it as a single `drawImageAt()` call:

```cpp
// On resize only:
fullNoiseImage = Image(ARGB, getWidth(), getHeight(), true);
// ... tile noise into fullNoiseImage once

// In paint():
g.setOpacity(noiseAlpha);
g.drawImageAt(fullNoiseImage, 0, 0);
```

**Estimated savings:** 1-2% GPU

#### 2C. Skip Hue Cycling When Change Is Below Threshold

Only regenerate the gradient when the hue has shifted enough to be visually distinct (e.g., every 0.5° instead of every frame):

```cpp
const float hueDelta = std::abs(hueOffset - lastPaintedHue);
if (hueDelta < 0.0014f) // ~0.5° of 360°
    return; // reuse cached image as-is
```

**Estimated savings:** 3-5% GPU (most frames skip gradient regen)

#### 2D. Throttle Knob Glow Timer

Reduce from 30 Hz to 15 Hz and add an explicit stop when all glows have decayed:

```cpp
void timerCallback() override {
    bool anyActive = false;
    for (auto& [ptr, state] : knobGlowStates) {
        if (state.glowAlpha > 0.0f) {
            state.glowAlpha -= decayRate;
            anyActive = true;
        }
    }
    if (!anyActive) stopTimer();
}
```

**Estimated savings:** 1-2% GPU

---

### Tier 3: Architectural Improvements

#### 3A. Dirty-Rect Repainting

Instead of `repaint()` (full bounds), use `repaint(dirtyRect)` to only invalidate changed regions. For PadGridComponent, only repaint the specific pad whose playhead moved:

```cpp
repaint(padBounds[changedPadIndex]);
```

#### 3B. Render Waveforms to Cached Images

Pre-render each pad's waveform + loop region to a cached `juce::Image` that only updates when the audio file, loop points, or zoom changes. In paint, draw the cached image and overlay only the dynamic elements (playhead line, selection highlight).

#### 3C. Disable Scanlines and Noise by Default

Make the Pixel Grid scanline effect and noise overlay opt-in theme settings rather than always-on. Most users won't notice the difference.

#### 3D. Consider `juce::VBlankAttachment` (JUCE 7.0.3+)

Replace manual timers with `VBlankAttachment` which synchronizes repaints to the display's actual refresh rate and avoids unnecessary frames.

---

## Estimated Impact Summary

| Optimization                    | Estimated GPU Savings |
| ------------------------------- | --------------------- |
| 1C. Background animator → 5 Hz  | 6-10%                 |
| 1B. Consolidate timers          | 3-5%                  |
| 2C. Hue threshold skip          | 3-5%                  |
| 2A. Cache dashed strokes        | 2-3%                  |
| 2B. Pre-composite noise         | 1-2%                  |
| 2D. Throttle knob glow          | 1-2%                  |
| **Combined estimate**           | **~15-20% reduction** |

With all Tier 1 + Tier 2 optimizations applied, GPU usage should drop from
~30% to approximately **10-15%** on M3 Max.
