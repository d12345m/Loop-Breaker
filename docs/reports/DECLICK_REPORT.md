# De-Click Post-Processing Stage — Feasibility Report

**Date:** 2026-03-05  
**Inspired by:** Max/MSP `~swanramp` (swan ramp / s-curve ramp for click-free sample transitions)  
**Scope:** Lightweight output-side de-clicking after buffer slicing/SoundTouch, before per-buffer FX

---

## 1. Problem Statement

Despite extensive crossfade systems already in place (input-side equal-power fades, output-side ring crossfade, lookahead pre-blending, mode-transition fades, SoundTouch priming fade-ins), **residual clicks persist** during certain playback conditions. These are low-level discontinuities that survive the existing mitigation because:

1. **They originate inside SoundTouch's OLA engine** — the overlap-add algorithm can internally create small transients when its correlation search picks suboptimal match positions, particularly during rapid parameter sweeps or just after boundary-related input discontinuities. The existing input-side crossfade smooths what _we_ feed SoundTouch, but can't control what SoundTouch's internal splicing does.

2. **Block-boundary discontinuities** — the last sample of block N and the first sample of block N+1 can be discontinuous when SoundTouch's internal buffer state shifts between callbacks, or when parameter smoothers haven't fully converged.

3. **File-boundary wrap in repitch mode** (`AudioBuffer.cpp` L689–L707) — when the playhead wraps from end-of-file back to 0.0, no crossfade is engaged. This is a known gap in the existing crossfade coverage.

4. **Sub-crossfade residuals** — even when a crossfade IS applied, if the audio content at the two crossfade endpoints is significantly different in amplitude or phase, the crossfade itself can produce a brief "bump" (energy spike from the overlap of non-correlated signals). This is especially audible with short crossfade lengths (20ms in repitch mode).

5. **SoundTouch priming edge cases** — the fade-in after priming masks the initial transient, but the boundary between "primed" and "normal" output can still carry a small discontinuity that the linear fade-in doesn't fully suppress.

---

## 2. What is `~swanramp` and Why It's Relevant

Max/MSP's `swanramp~` (also known as swan ramp or S-curve ramp) is used in sampling instruments to apply an **S-shaped (sigmoid) envelope micro-ramp** at discontinuity points. Unlike a linear crossfade which blends two signals, `swanramp~` works on a **single signal** by detecting where a discontinuity occurs (or is about to occur) and applying a very short gain envelope that:

- **Fades the signal down** through the discontinuity using an S-curve (not linear — the curve has zero derivative at both endpoints, so it doesn't introduce new discontinuities of its own)
- **Fades back up** on the other side

The S-curve shape (typically a raised cosine or Hann window segment) is critical: a linear ramp to zero would itself create a discontinuity in the first derivative (acceleration), which is audible as a softer but still present click. The S-curve has zero slope at both the start and end of the ramp, making it perceptually transparent.

**Key insight:** `swanramp~` doesn't need a second signal to crossfade with. It works as a post-hoc safety net on whatever audio is already there.

---

## 3. Proposed Approach: Adaptive Micro-Fade De-Clicker

### 3.1 Core Algorithm

A lightweight **block-boundary and intra-block discontinuity smoother** that operates on the final output of each `AudioBuffer::processBlock()`, after all existing crossfade mechanisms but before the audio reaches the FX chain and mix bus.

**Two complementary mechanisms:**

#### A. Block-Boundary Micro-Ramp (Swan Ramp Style)

At every `processBlock()` boundary, compare the **last sample of the previous block** (already tracked in `lastOutputSample[ch]`) with the **first sample of the current block**. If the delta exceeds a threshold:

1. Apply a short **S-curve fade-out** to the first N samples of the current block (ramp from attenuated → full)
2. The ramp shape is a **raised cosine** (Hann window half): `gain(i) = 0.5 * (1.0 - cos(π * i / N))`
3. N is **adaptive**: proportional to the magnitude of the discontinuity, clamped to a range (e.g., 8–64 samples, i.e., 0.18ms–1.45ms at 44.1kHz)

This is the digital equivalent of what `swanramp~` does, applied only where needed.

#### B. Intra-Block Discontinuity Scan (Optional, Higher CPU)

Scan the output buffer for sample-to-sample deltas exceeding a threshold. When found, apply a symmetric micro-ramp (centered on the discontinuity point) that dips the gain and recovers:

1. At discontinuity index `d`, apply the ramp from `d - halfWidth` to `d + halfWidth`
2. Shape: Hann dip — `gain(i) = 1.0 - depth * (0.5 + 0.5 * cos(2π * (i - d) / width))`
3. `depth` scales with the discontinuity magnitude (small delta → shallow dip; large delta → deeper dip, never fully to zero)

This catches intra-block clicks from SoundTouch's internal OLA splicing.

### 3.2 Implementation Shape (Pseudo-Code)

```cpp
// Called at end of AudioBuffer::processBlock(), after mode-transition crossfade
void AudioBuffer::applyDeClick(juce::AudioBuffer<float>& outputBuffer)
{
    const int numSamples = outputBuffer.getNumSamples();
    const int numChannels = outputBuffer.getNumChannels();

    // --- A. Block-boundary micro-ramp ---
    for (int ch = 0; ch < juce::jmin(numChannels, 2); ++ch)
    {
        float* data = outputBuffer.getWritePointer(ch);
        const float prevSample = deClickLastSample[ch];
        const float delta = std::abs(data[0] - prevSample);

        if (delta > deClickThreshold)
        {
            // Adaptive ramp length: longer for bigger discontinuities
            const int rampLen = juce::jlimit(kMinRampSamples, kMaxRampSamples,
                                             (int)(delta * kRampScaleFactor));
            const int actualRamp = juce::jmin(rampLen, numSamples);

            for (int i = 0; i < actualRamp; ++i)
            {
                // Raised cosine (Hann half-window): zero slope at boundaries
                const float t = (float)(i + 1) / (float)(actualRamp + 1);
                const float gain = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * t));
                data[i] *= gain;
            }
        }

        // Store last sample for next block
        deClickLastSample[ch] = data[numSamples - 1];
    }

    // --- B. Intra-block scan (optional) ---
    if (deClickIntraBlockEnabled)
    {
        for (int ch = 0; ch < juce::jmin(numChannels, 2); ++ch)
        {
            float* data = outputBuffer.getWritePointer(ch);
            for (int i = 1; i < numSamples; ++i)
            {
                const float d = std::abs(data[i] - data[i - 1]);
                if (d > deClickIntraThreshold)
                {
                    // Apply symmetric Hann dip centered at discontinuity
                    const int halfW = kIntraRampHalfWidth;
                    const int start = juce::jmax(0, i - halfW);
                    const int end   = juce::jmin(numSamples - 1, i + halfW);
                    const float depth = juce::jlimit(0.0f, 0.85f, d * kIntraDepthScale);

                    for (int j = start; j <= end; ++j)
                    {
                        const float phase = (float)(j - i) / (float)halfW; // -1..+1
                        const float env = 1.0f - depth * 0.5f * (1.0f + std::cos(juce::MathConstants<float>::pi * phase));
                        data[j] *= env;
                    }
                }
            }
        }
    }
}
```

### 3.3 Tuning Constants

| Constant                 | Proposed Value          | Rationale                                                                                                                   |
| ------------------------ | ----------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `kDeClickThreshold`      | 0.05 (≈ -26 dBFS delta) | Below this, discontinuities are inaudible. Matches the existing `kMinorDiscontinuityThreshold` in the tearing debug system. |
| `kMinRampSamples`        | 8 (0.18ms @ 44.1kHz)    | Shortest useful ramp — enough to suppress a click without audible volume modulation.                                        |
| `kMaxRampSamples`        | 64 (1.45ms @ 44.1kHz)   | Longest ramp for severe discontinuities. Beyond this, the ramp becomes audible as a volume dip.                             |
| `kRampScaleFactor`       | 128                     | Maps delta magnitude to ramp length: `delta=0.05 → 6 samples`, `delta=0.5 → 64 samples`.                                    |
| `kIntraRampHalfWidth`    | 4 samples (0.09ms)      | Very tight dip for intra-block clicks — must be narrow to avoid smearing audio.                                             |
| `kIntraDepthScale`       | 1.5                     | Scales delta to dip depth: `delta=0.3 → depth=0.45`, `delta=0.6 → depth=0.85 (clamped)`.                                    |
| `kDeClickIntraThreshold` | 0.15 (≈ -16 dBFS delta) | Higher threshold for intra-block since these are SoundTouch artifacts, not boundary jumps.                                  |

---

## 4. Latency Analysis

| Component                   | Added Latency | Notes                                                                                                               |
| --------------------------- | ------------- | ------------------------------------------------------------------------------------------------------------------- |
| **Block-boundary ramp (A)** | **0 samples** | Operates in-place on the current block. No lookahead required — the previous block's last sample is already stored. |
| **Intra-block scan (B)**    | **0 samples** | In-place modification within the current block.                                                                     |
| **Total**                   | **0 samples** | Zero added latency. No buffering, no lookahead.                                                                     |

The approach is purely reactive: it detects discontinuities that have already occurred and smooths them within the same block. This is possible because audio clicks are instantaneous — by the time we detect the large delta at sample `i`, we can still modify samples `i` through `i + rampLen` within the same block to smooth the transition.

The block-boundary case is slightly different: we detect the discontinuity between `lastOutputSample` and `data[0]`, then apply a fade-in ramp from `data[0]` onward. This means the very first sample after a discontinuity is attenuated, but at 0.18ms minimum ramp time, this is perceptually identical to the `swanramp~` approach.

---

## 5. CPU Cost Analysis

### Block-Boundary Ramp (A) — Per Block

| Operation                                                         | Cost                                            |
| ----------------------------------------------------------------- | ----------------------------------------------- |
| 2× `abs()` + 2× comparison                                        | Negligible                                      |
| Ramp (worst case): 64 samples × 2 channels × (1 cos + 1 multiply) | ~256 FP ops                                     |
| **Per-block overhead**                                            | ~256 FP ops worst case, 0 when no discontinuity |

At 44.1kHz / 512 sample blocks ≈ 86 blocks/sec → **~22,000 FP ops/sec worst case** (essentially free).

In practice, the ramp only fires on blocks where a discontinuity actually occurs, so avg cost is much lower.

### Intra-Block Scan (B) — Per Block

| Operation                                                                | Cost                                           |
| ------------------------------------------------------------------------ | ---------------------------------------------- |
| Scan: `numSamples × 2 channels × (1 abs + 1 compare)`                    | ~2048 FP ops per block                         |
| Smoothing (per detected click): `2 × halfWidth × (1 cos + 2 multiplies)` | ~32 FP ops per click                           |
| **Per-block overhead**                                                   | ~2048 FP ops baseline + ~32 per detected click |

At 86 blocks/sec → **~176,000 FP ops/sec** for the scan. This is about **0.001%** of a modern CPU core's throughput (>10 billion FLOPS). Negligible.

### Comparison to Existing Costs

For reference, SoundTouch's OLA processing costs ~500,000–2,000,000 FP ops per block. The de-click stage adds less than 0.1% overhead on top of that.

---

## 6. Insertion Point

The ideal insertion point is inside `AudioBuffer::processBlock()`, **after the mode-transition crossfade** (L297) and **before the `previousBlockBuffer` cache** (L302):

```
processWithRepitching() or processWithTimeStretch()
    ↓
mode-transition crossfade (existing, L255–L297)
    ↓
>>> NEW: applyDeClick(outputBuffer) <<<
    ↓
cache previousBlockBuffer (L302–L315)
    ↓
tearing debug validation (L320+)
    ↓
return to AudioBufferManager::processSingleBuffer()
    ↓
perBufferProcessor callback (FX chain)
    ↓
gain staging
```

This position ensures:

- All existing crossfade mechanisms have already run (we're the final safety net)
- The `previousBlockBuffer` cache captures the de-clicked output (so mode-transition crossfades in the NEXT block use clean audio)
- The tearing debug system sees the de-clicked output (accurate diagnostics)
- The FX chain receives clean audio (no clicks propagated into reverb/delay tails)

---

## 7. Comparison to Alternative Approaches

| Approach                                        | Latency   | CPU      | Effectiveness                                                       | Complexity |
| ----------------------------------------------- | --------- | -------- | ------------------------------------------------------------------- | ---------- |
| **Proposed: Adaptive micro-fade**               | 0 samples | Very low | High for boundary clicks, medium for OLA artifacts                  | Low        |
| One-pole lowpass on output                      | 0 samples | Very low | Low — smooths everything, dulls transients                          | Very low   |
| Short IIR median filter                         | 0 samples | Low      | Medium — good for impulse noise, but distorts audio at filter edges | Medium     |
| Overlap-add output reprocessing                 | 1 block   | Medium   | High                                                                | High       |
| Full crossfade output buffer (double buffering) | 1 block   | Medium   | Very high                                                           | High       |
| Zero-crossing gated ramp                        | 0 samples | Low      | Medium — only works near zero crossings                             | Medium     |

The proposed approach offers the best latency/effectiveness trade-off. It's also the most architecturally aligned with the existing codebase, which already uses similar techniques (equal-power crossfades, Hann windows) throughout.

---

## 8. Risk Assessment

| Risk                                                               | Likelihood | Impact   | Mitigation                                                                                        |
| ------------------------------------------------------------------ | ---------- | -------- | ------------------------------------------------------------------------------------------------- |
| Threshold too low → audible volume modulation ("pumping")          | Medium     | Medium   | Expose threshold as a tunable constant; start conservative (0.05)                                 |
| Threshold too high → clicks pass through                           | Low        | Low      | The existing crossfade systems catch most clicks; this is a safety net                            |
| Intra-block scan alters intentional transients (drums, attacks)    | Low        | Medium   | Use higher threshold for intra-block (0.15); disable for repitch-only mode where clicks are rarer |
| Interaction with tearing debug → false negative masking            | Low        | Low      | Run tearing debug BEFORE de-click so diagnostics reflect raw output                               |
| S-curve ramp on very first sample introduces low-level DC artifact | Very low   | Very low | The ramp starts from the attenuated value, not from zero — no DC step                             |

---

## 9. Feasibility Verdict

**FEASIBLE — strongly recommended.**

- **Zero latency** added
- **Negligible CPU cost** (~0.001% of available processing)
- **Architecturally clean** insertion point exists
- **Non-destructive** — only activates when discontinuities are detected
- **Complementary** to existing crossfade systems (safety net, not replacement)
- **Well-precedented** — `swanramp~` has been proven in Max/MSP sampling instruments for exactly this purpose
- **Low risk** — conservative thresholds prevent audible artifacts; can be disabled with a single boolean

The block-boundary ramp (Part A) alone would address the most common residual clicks. The intra-block scan (Part B) is optional and can be added later if needed.

---

## 10. Implementation Checklist

### Phase 1: Core Infrastructure (Block-Boundary De-Click)

- [ ] **1.1** Add de-click member variables to `AudioBuffer.h`
  - `float deClickLastSample[2] = { 0.0f, 0.0f };` — tracks last output sample for boundary detection
  - `bool deClickEnabled = true;` — master enable flag
  - Constants: `kDeClickThreshold`, `kMinRampSamples`, `kMaxRampSamples`, `kRampScaleFactor`
  - File: `Source/AudioBuffer.h`, private section (~L341, near existing `lastOutputSample`)

- [ ] **1.2** Implement `applyDeClick()` method — block boundary ramp only (Part A)
  - Per-channel: compare `deClickLastSample[ch]` with `data[0]`
  - If delta > threshold: compute adaptive ramp length, apply raised-cosine fade-in
  - Update `deClickLastSample[ch]` at end of block
  - File: `Source/AudioBuffer.cpp`, new method (add near existing crossfade helpers, ~L2640)

- [ ] **1.3** Wire `applyDeClick()` into `processBlock()`
  - Insert call after mode-transition crossfade (after L297) and before `previousBlockBuffer` cache (before L302)
  - File: `Source/AudioBuffer.cpp`, inside `processBlock()` (~L298)

- [ ] **1.4** Reset de-click state in `prepare()` and `resetToDefaults()`
  - Zero out `deClickLastSample[]` in `prepare()` (~L166) and `resetToDefaults()` (~L1006)
  - File: `Source/AudioBuffer.cpp`

- [ ] **1.5** Build and test Phase 1
  - Verify no new compiler warnings
  - Test with known click-producing scenarios: file boundary loop (repitch), rapid slice changes (stretch), mode transitions
  - Monitor tearing debug panel — discontinuity counts should decrease
  - Listen for any audible volume modulation ("pumping") at the block boundary

### Phase 2: Intra-Block Scan (Optional Enhancement)

- [ ] **2.1** Add intra-block scan constants to `AudioBuffer.h`
  - `bool deClickIntraBlockEnabled = false;` — off by default until validated
  - Constants: `kDeClickIntraThreshold`, `kIntraRampHalfWidth`, `kIntraDepthScale`

- [ ] **2.2** Implement intra-block scan in `applyDeClick()` (Part B)
  - Linear scan for deltas exceeding `kDeClickIntraThreshold`
  - Apply symmetric Hann dip at each detected point
  - Guard against overlapping dips (skip if within `2 * halfWidth` of a previous dip)

- [ ] **2.3** Build and test Phase 2
  - Test with SoundTouch active + slicing + pitch shift (highest click density scenario)
  - A/B test: enable vs disable intra-block scan
  - Profile CPU impact with all 8 buffers active
  - Verify drum transients are preserved (use a drum loop test file)

### Phase 3: Fix Known Crossfade Gap

- [ ] **3.1** Add crossfade for file-boundary wrap in repitch mode
  - In `processWithRepitching()`, at L689–L707 (file boundary section), add `isInCrossfade = true; crossfadePosition = 0; previousSlicePlayheadPos = currentPos;` before `currentPos = 0.0` (forward) or `currentPos = fileLengthSamples - 1` (reverse)
  - This is the same pattern already used for loop-window wraps at L710–L739
  - File: `Source/AudioBuffer.cpp`, ~L689–L707

- [ ] **3.2** Build and test Phase 3
  - Test file looping in repitch mode with no slicing — should now be click-free
  - Verify the crossfade doesn't create a volume dip at the loop point

### Phase 4: Tuning & Polish

- [ ] **4.1** Run comprehensive test matrix
  - All modifier combinations: speed × stretch × pitch × reverse × slicing × ping-pong
  - All sample rates: 44.1k, 48k, 88.2k, 96k
  - All block sizes: 64, 128, 256, 512, 1024
  - Edge cases: very short files, single-sample slices, extreme speed values

- [ ] **4.2** Tune thresholds based on test results
  - Adjust `kDeClickThreshold` if too many/few clicks caught
  - Adjust ramp lengths if audible pumping or insufficient smoothing
  - Consider making thresholds sample-rate-relative if needed

- [ ] **4.3** Add de-click toggle to debug panel (optional)
  - Add an enable/disable toggle in `TearingDebugPanel.h`
  - Display de-click activation count for diagnostics
  - File: `Source/TearingDebugPanel.h`

- [ ] **4.4** Reorder tearing debug to run BEFORE de-click (diagnostic accuracy)
  - Move the tearing debug validation block to run before `applyDeClick()` in `processBlock()`
  - This way, the debug panel reports raw discontinuities, and we can see how many the de-clicker would have caught
  - Alternatively, add a separate counter for "de-click activations" alongside existing tearing stats

- [ ] **4.5** Document the de-click system
  - Add inline comments explaining the algorithm, threshold choices, and `swanramp~` inspiration
  - Update any relevant design docs
