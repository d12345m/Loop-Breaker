# Speed / Stretch Modifier Conflict Analysis

**Date:** 2026-02-10  
**Scope:** Investigate whether the Speed and Stretch modifiers conflict, cause audio tearing, or have timing/performance issues when used together or in sequence.

---

## 1. Architecture Summary

The codebase has **two completely independent audio processing paths** inside `AudioBuffer::processBlock()`:

| Path | Trigger | Engine | Pitch changes? | Controlled by |
|------|---------|--------|----------------|---------------|
| **Repitching** (`processWithRepitching`) | `stretchRatio == 1.0` | Manual sample interpolation | Yes (pitch scales with speed) | `params.speed`, `tempoMultiplier` |
| **Time-Stretch** (`processWithTimeStretch`) | `stretchRatio != 1.0` | SoundTouch (`setTempo()`) | No (pitch preserved) | `stretchRatio`, direction from `params.speed` sign only |

The mode switch happens every `processBlock()` call at `AudioBuffer.cpp` lines 52–65:

```cpp
const bool useStretch = std::abs(stretch - 1.0) > 1.0e-6;
if (useStretch)
    processWithTimeStretch(outputBuffer);
else
    processWithRepitching(outputBuffer);
```

**Key finding:** These paths are mutually exclusive per block. There is no blended mode.

---

## 2. Confirmed Conflict Points

### 2.1 Speed Magnitude Is Silently Discarded When Stretching

When `stretchRatio != 1.0`, the stretch path takes over. Inside `processWithTimeStretch()` (line 432):

```cpp
const double direction = (params.speed < 0.0 ? -1.0 : 1.0);
const double inputSpeed = direction * tempoMultiplier.load();
```

**The magnitude of `params.speed` (e.g., 0.25, 0.5, 2.0) is completely ignored.** Only its sign (forward/reverse) is used. This means:

- If a **Speed(2.0x)** modifier fires, setting `params.speed = 2.0`, playback is 2x faster with pitch shift.
- If a **Stretch(0.5x)** modifier then fires on the same buffer, the stretch path takes over. The speed magnitude (2.0) is silently dropped. Effective playback is now 0.5x tempo (SoundTouch), not 2.0x × 0.5x = 1.0x as a user might expect.
- **Neither modifier clears the other's state.** `applySpeed()` doesn't reset `stretchRatio`, and `applyStretch()` doesn't reset `params.speed`.

This creates a **hidden state conflict**: the buffer remembers `speed=2.0` and `stretchRatio=0.5`, but only one is active at a time. When Stretch is later reset to 1.0 (via ResetAll or another modifier), Speed 2.0x suddenly reappears with no user action.

### 2.2 Mode Transition Causes Audible Discontinuity (Tearing Source)

When switching between repitch and stretch modes, a 10ms fade-in is applied (lines 69–76):

```cpp
if (useStretch != lastBlockUsedStretch)
{
    const int fadeSamples = juce::jmin(outputBuffer.getNumSamples(),
                                       juce::jmax(1, (int)(hostSampleRate * 0.01))); // 10ms
    for (int ch = 0; ch < outputBuffer.getNumChannels(); ++ch)
    {
        float* data = outputBuffer.getWritePointer(ch);
        for (int i = 0; i < fadeSamples; ++i)
            data[i] *= (float)(i + 1) / (float)(fadeSamples + 1);
    }
}
```

**Problems with this approach:**

1. **Fade-in only, no fade-out of the previous block.** The last block before the transition plays out at full volume, then the new block starts from near-zero. This creates a **hard edge followed by a ramp** — exactly the "tearing" sound described.
2. **The playhead position jumps.** The repitch path advances the playhead by `speed × (fileSampleRate / hostSampleRate)` per sample. The stretch path advances by `inputSpeed × (fileSampleRate / hostSampleRate)` per sample (where `inputSpeed` uses only direction). When transitioning from `speed=2.0` repitch to stretch, the per-sample advancement changes from `2.0 × ratio` to `1.0 × ratio`. **The playhead skips or appears to stutter**, and the audio content at the transition point is discontinuous.
3. **SoundTouch internal buffer reset.** On mode transition, `stretcherNeedsReset` is set (line 365), which clears SoundTouch's internal pipeline. The stretcher then needs to be re-primed, causing a **burst of silence or underrun** on the first stretch block. The fade-in masks some of this but not all.

### 2.3 SoundTouch Underruns During First Blocks After Transition

After a reset, SoundTouch's internal FIFO is empty. The feed/drain loop (lines 742–773) tries up to 8 iterations to fill the output block. If it cannot fill the entire block:

- A fade-to-zero is applied to the remaining samples (lines 795–810).
- `timeStretchUnderfills` counter increments.

**These underruns produce brief silence gaps** (a few ms), which are audible as clicks or tearing, especially at buffer sizes < 256 samples. The 20ms fade-in after priming (`stretchFadeInRemaining`, line 730) helps on the first block but doesn't prevent underruns on subsequent blocks if SoundTouch's latency hasn't settled.

### 2.4 Crossfade Logic Is Not Stretch-Aware

`applyCrossfadeToSliceTransition()` (line 1055) uses `getEffectiveSpeed()` for computing the previous-slice sample position:

```cpp
const double effectiveSpeed = getEffectiveSpeed();
const double prevPos = previousSlicePlayheadPos + sample * effectiveSpeed * (fileSampleRate / hostSampleRate);
```

`getEffectiveSpeed()` returns `params.speed * tempoMultiplier`, which does **not** account for the stretch ratio. When stretching, the actual playhead advancement is different from what `getEffectiveSpeed()` returns. This means:

- **Crossfade reads from wrong positions** in the source buffer during stretch mode.
- The crossfade output blends samples that don't correspond to the actual playback trajectory, creating **brief tonal glitches or phasing artifacts** during slice transitions while stretching.

### 2.5 `resetToDefaults()` Resets Both, But `applyReset()` Doesn't Coordinate

`AudioBuffer::resetToDefaults()` (line 375) properly resets both:
```cpp
params.reset();          // speed → 1.0
stretchRatio.store(1.0); // stretch → 1.0
```

However, `AppState::applyReset()` calls `channelStrips[idx]->reset()` which calls `resetToDefaults()`. **This is correct.** But individual Speed or Stretch modifiers do not clear each other, creating the hidden state issue in §2.1.

### 2.6 Speed Smoother Not Used in Stretch Path

In the repitch path, speed changes are smoothed via `speedSmoother` (128-sample window):
```cpp
speedSmoother.setTargetValue(getEffectiveSpeed());
```

In the stretch path, tempo changes are applied **instantly** via `stretcher.setTempoRatio()`. SoundTouch does its own internal windowed processing, but the _input read speed_ changes instantly. **There is no smoothing of the `stretchRatio` parameter itself**, which can cause audible artifacts if the ratio changes mid-playback (e.g., rapid scheduler triggers).

---

## 3. SoundTouch Rate API: Should Speed Go Through SoundTouch?

### 3.1 How SoundTouch Handles Rate vs Tempo vs Pitch

SoundTouch internally computes effective parameters from three "virtual" knobs:

```
effective_tempo = virtualTempo / virtualPitch
effective_rate  = virtualPitch * virtualRate
```

| API | What it does | Pitch changes? | Uses time-stretch? |
|-----|-------------|----------------|-------------------|
| `setTempo()` | Changes playback speed without changing pitch | No | Yes (TDHS algorithm) |
| `setRate()` | Changes playback speed with proportional pitch change | Yes | No (resampling only) |
| `setPitch()` | Changes pitch without changing speed | Pitch only | Yes + resampling |

**The current wrapper (`TimeStretchSoundTouch.h`) only exposes `setTempo()`.** It never calls `setRate()` or `setPitch()`.

### 3.2 Could Speed Be Routed Through SoundTouch's `setRate()`?

**Yes, this is technically feasible and would provide several benefits:**

**Benefits:**
1. **Unified audio pipeline.** Both speed and stretch would go through one engine, eliminating the mode-switch discontinuity entirely.
2. **Combining speed + stretch becomes natural.** SoundTouch is designed to handle `setRate()` and `setTempo()` simultaneously. For example, `setRate(2.0)` + `setTempo(0.5)` gives 2x playback speed (with pitch shift) at half tempo — SoundTouch handles the math internally.
3. **No more hidden state.** The wrapper could expose `setRate()` for the Speed modifier and `setTempo()` for the Stretch modifier, and SoundTouch would combine them automatically.
4. **Smoother transitions.** SoundTouch's overlap-add windowing handles gradual parameter changes more gracefully than the current hard mode switch.

**Costs / Risks:**
1. **Always-on SoundTouch overhead.** Currently, SoundTouch is bypassed when `stretchRatio == 1.0`. Routing speed through it means SoundTouch processes every block, even for simple rate changes. At `rate=1.0, tempo=1.0`, SoundTouch is essentially a pass-through, but it still adds ~20ms latency and some CPU.
2. **Latency increase.** SoundTouch's TDHS algorithm introduces `SETTING_INITIAL_LATENCY` samples of latency (~60ms with current settings). The repitch path has zero added latency. Always routing through SoundTouch would add perceptible latency for speed-only changes.
3. **Quality difference for pure rate changes.** The current repitch path uses simple linear interpolation (per-sample). SoundTouch's rate transposer is higher quality but also heavier. For simple 2x or 0.5x speeds, the current approach may actually sound fine.
4. **Reverse playback complexity.** The current code feeds reversed samples into SoundTouch by reading the source buffer backwards. SoundTouch's rate transposer doesn't natively support negative rates. This would need to stay as-is.

### 3.3 Recommendation

**A hybrid approach is recommended** rather than routing all speed through SoundTouch:

- When **only Speed is active** (stretch == 1.0): keep the lightweight repitch path (zero latency, low CPU).
- When **only Stretch is active** (speed == 1.0 or ±1.0): keep the SoundTouch tempo path.
- When **both Speed and Stretch are active**: use SoundTouch's `setRate()` for the speed component and `setTempo()` for the stretch component simultaneously. This eliminates the mode conflict while keeping the lightweight path for the common case.

This would require extending `TimeStretchSoundTouch` to expose `setRate()`.

---

## 4. Performance Considerations

### 4.1 CPU Cost of Mode Switching

Every `processBlock()` call evaluates `stretchRatio` and decides the path. The mode switch itself is cheap (a float comparison), but the **consequences** (SoundTouch reset, re-priming, fade-in) add CPU spikes on transition blocks.

### 4.2 SoundTouch Feed Loop

The current feed/drain loop (`while (framesWritten < numOutputSamples && safetyIters++ < 8)`) is well-optimized with proportional feed sizing. However, after a reset, the first 2–3 iterations may underrun, causing brief CPU spikes and potential audio dropouts at small buffer sizes.

### 4.3 Atomic Contention

`stretchRatio`, `playheadPosition`, and `tempoMultiplier` are all `std::atomic<double>`. In the stretch path's `fillInputScratch` lambda, `playheadPosition` is read once and written once (optimization noted in code comments). This is acceptable, but the lack of a single coherent snapshot of all three values means **rare races where the mode decision and the parameter values disagree** by one block.

---

## 5. Reproduction Scenario for Tearing

Based on the analysis, the tearing is most likely to occur when:

1. A buffer is playing with **Speed != 1.0** (e.g., Speed 0.5x modifier fired).
2. A **Stretch modifier fires** on the same buffer (e.g., Stretch 2.0x).
3. The system transitions from repitch → stretch in one `processBlock()` call.
4. The transition causes:
   - SoundTouch reset (clearing internal buffers).
   - A playhead position that was advancing at 0.5x now advances at 1.0x (direction only).
   - A hard edge in audio content + a 10ms fade-in ramp.
   - Possible 1–2 blocks of underrun silence while SoundTouch primes.
5. If a **ResetAll** then fires, `stretchRatio` goes back to 1.0, causing another stretch → repitch transition with the same issues in reverse.

The intermittent nature matches: it only tears **when both modifiers target the same buffer in sequence**, which depends on random scheduler selection and target assignment.

---

## 6. Detailed TODO List

### Critical (Likely causing the tearing)

- [ ] **TODO-1: Add crossfade on mode transitions.** Replace the fade-in-only approach (AudioBuffer.cpp lines 69–76) with a proper crossfade that renders the _tail_ of the outgoing mode alongside the _head_ of the incoming mode. This requires buffering the last N samples of the previous block. (~50 lines of code)

- [ ] **TODO-2: Coordinate speed/stretch state on modifier application.** In `AppState::applySpeed()`, reset `stretchRatio` to 1.0 so the repitch path takes clean control. In `AppState::applyStretch()`, reset `params.speed` to ±1.0 (preserving direction) so the stretch path doesn't inherit stale speed magnitude. (~10 lines of code)

- [ ] **TODO-3: Fix crossfade position computation in stretch mode.** In `applyCrossfadeToSliceTransition()`, detect when the stretch path is active and use the actual input advancement rate (`direction * tempoMultiplier * fileSampleRate / hostSampleRate`) instead of `getEffectiveSpeed()`. (~5 lines of code)

- [ ] **TODO-4: Smooth stretchRatio transitions.** Add a `SmoothedValue<double>` for `stretchRatio` similar to `speedSmoother`, or at minimum apply a short fade when the ratio changes by more than a threshold. This prevents abrupt tempo jumps mid-block. (~15 lines of code)

### Important (Improving robustness)

- [ ] **TODO-5: Extend `TimeStretchSoundTouch` to expose `setRate()`.** Add a `setRateRatio(float)` method that calls `st.setRate()`. This enables the combined speed+stretch path described in §3.3. (~10 lines in TimeStretchSoundTouch.h)

- [ ] **TODO-6: Implement combined speed+stretch SoundTouch path.** When both `params.speed != ±1.0` and `stretchRatio != 1.0`, route through SoundTouch with both `setRate()` and `setTempo()` active. Update `processBlock()` routing logic. (~40 lines of code)

- [ ] **TODO-7: Increase SoundTouch priming to cover latency.** After a reset, pre-feed `getLatencySamples()` worth of input before attempting to drain output. This eliminates the 1–2 block underrun window. The current code explicitly avoids this ("No expensive priming") but the underruns are worse than the one-time priming cost. (~15 lines of code)

- [ ] **TODO-8: Add a fade-out tail before SoundTouch reset.** Before clearing SoundTouch buffers on mode transition, drain remaining output and apply a fade-out, so the old mode's audio gracefully decays rather than being hard-cut. (~20 lines of code)

### Nice-to-Have (Quality of life)

- [ ] **TODO-9: Log mode transitions for debugging.** Add a `DBG()` trace when `useStretch != lastBlockUsedStretch`, printing the old/new speed, stretch, and playhead position. This will help reproduce and diagnose future tearing reports. (~5 lines of code)

- [ ] **TODO-10: Audit `getEffectiveSpeed()` usage.** Search all call sites of `getEffectiveSpeed()` and verify they are only used in the repitch path. If any are used in stretch-path contexts, they return wrong values. (Audit task, ~30 minutes)

- [ ] **TODO-11: Consider a "speed lock" UI indicator.** When stretch is active, the UI should visually indicate that the Speed modifier's magnitude is being ignored. This prevents user confusion about why speed changes don't seem to take effect. (UI task)

- [ ] **TODO-12: Add unit test for speed→stretch→reset sequence.** Write a `ModifierSchedulerTests` case that applies Speed(2.0), then Stretch(0.5), then ResetAll to the same buffer, and verifies the output doesn't contain discontinuities (zero-crossing check or RMS-delta check). (~40 lines of test code)

---

## 7. Summary

| Issue | Severity | Likely Causes Tearing? |
|-------|----------|----------------------|
| Speed magnitude silently dropped in stretch mode | High | Indirect (hidden state → unexpected jumps on reset) |
| Mode transition: fade-in only, no crossfade | **Critical** | **Yes — hard audio edge** |
| SoundTouch underrun after reset | High | **Yes — brief silence gaps** |
| Crossfade uses wrong speed in stretch mode | Medium | Yes — glitchy slice transitions while stretching |
| No smoothing on stretchRatio changes | Medium | Yes — abrupt tempo jumps |
| Neither modifier resets the other's state | High | Indirect (compounds the above issues) |
