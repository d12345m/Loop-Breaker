# Audio Performance Analysis & Optimization Report

**Date:** 2025-02-25  
**Scope:** Full performance audit — buffer management, effects chain, SoundTouch feeding, sample rate handling, threading, and dropout risk.

---

## 1. Executive Summary

The plugin runs **8 simultaneous AudioBuffer instances**, each with its own SoundTouch time-stretcher and **10-stage per-buffer effects chain** (delay, HPF, LPF, tremolo, chorus, auto-pan, ducking, volume ramp, limiter, reverb). All 8 buffers are processed serially on a single audio thread callback. The combined per-block workload is significant and represents the primary risk factor for audio dropouts, especially at lower buffer sizes (64–128 samples) or higher sample rates (96 kHz+).

### Key Findings

| Priority | Issue | Impact |
|----------|-------|--------|
| 🔴 Critical | Per-sample processing loops in repitch path & ChannelStrip effects | CPU scaling, dropout risk |
| 🔴 Critical | 8 × SoundTouch instances running simultaneously (worst case) | Extreme CPU load |
| 🟠 High | SpinLock contention on `getAudioDataSnapshot()` — called multiple times per buffer per block | Lock contention in realtime thread |
| 🟠 High | No sample-rate resampling of loaded audio — file SR ≠ host SR causes interpolation overhead every sample | Wasted CPU, quality loss |
| 🟠 High | Effects chain: per-sample inner loops with branching (not vectorized) | Prevents SIMD optimization |
| 🟡 Medium | No limit on accepted sample rates | 96/192 kHz doubles/quadruples all workloads |
| 🟡 Medium | SoundTouch feed loop can iterate up to 24 times per block | Worst-case CPU spikes |
| 🟡 Medium | Debug-only tearing detection runs per-sample analysis in `#if JUCE_DEBUG` | Debug builds orders of magnitude slower |
| 🟡 Medium | Memory allocation on audio thread (scratch buffer resizing) | Occasional allocation stalls |
| ⚪ Low | Unity build of SoundTouch .cpp files in PluginProcessor.cpp | Compilation/optimization concern |

---

## 2. SoundTouch Buffer Feeding Analysis

### 2.1 How JUCE Feeds SoundTouch

The feeding pipeline works as follows:

1. **PluginProcessor::processBlock** receives a JUCE `AudioBuffer<float>` from the host (typically 64–2048 samples).
2. For each of 8 pads, `AudioBufferManager::processSingleBuffer` calls `AudioBuffer::processBlock`.
3. `AudioBuffer::processBlock` decides: repitch path or SoundTouch path based on `StretchSnapshot`.
4. In the SoundTouch path (`processWithTimeStretch`):
   - A `fillInputScratch` lambda reads from the loaded audio data (with interpolation, slice handling, loop window enforcement, crossfading — all **per-sample**) into `stretchInScratch`.
   - The filled scratch is interleaved into a flat buffer and pushed to SoundTouch via `putSamples`.
   - Output is deinterleaved back to JUCE channel pointers.
   - A **while loop** (up to 24 iterations) alternately drains and feeds SoundTouch until the output block is full.

### 2.2 Identified Problems

**Problem A: Over-feeding with per-sample source reading**

The `fillInputScratch` function processes input **one sample at a time** with per-sample:
- Atomic loads (`loopWindowEnabled`, `slicingModeActive`, `pingPongEnabled`, `playheadPosition`)
- Branching for slice boundaries, loop windows, ping-pong direction
- Linear interpolation (two reads + lerp per channel)
- Crossfade application when active

At high effective rates (e.g., speed=2x + stretch=2x), the feed function may need to generate 4x the output block size in input samples — all per-sample. For a 512-sample block at 2x combined rate, that's 1024+ iterations of this inner loop, **per buffer**, **times 8 buffers**.

**Recommendation A1:** Add a **block-based fast path** for the common case where no slicing, no loop window, no crossfade, and no ping-pong is active. The existing `canBulkCopy` fast path only triggers when `inputSpeed == 1.0` AND `fileSR == hostSR`. Extend it to handle `inputSpeed != 1.0` by using a block-level resampling approach (e.g., advancing the playhead by `step * numFrames` and using `juce::LagrangeInterpolator` or similar to generate the N frames in one call).

**Recommendation A2:** Reduce atomic traffic in the inner loop. Snapshot all atomic state (`loopWindowEnabled`, `loopStart`, `loopEnd`, `slicingModeActive`, `pingPongEnabled`, `pingPongPeriodSamples`, etc.) once at the top of `fillInputScratch`, not per-sample.

**Problem B: Interleave/deinterleave overhead**

SoundTouch requires interleaved audio. `TimeStretchSoundTouch::processNonInterleaved` manually interleaves input and deinterleaves output with per-sample loops. For stereo, this is 2 × N copies for input and 2 × N copies for output.

**Recommendation B:** Consider using `juce::AudioDataConverters` or SIMD-friendly interleave/deinterleave. For stereo specifically, an SSE2/NEON intrinsic pair could halve the cycle count. Alternatively, if SoundTouch processing remains the bottleneck, consider maintaining an interleaved representation of loaded audio data to eliminate the input interleave step entirely.

**Problem C: Feed loop iteration count**

The feed loop cap is 24 iterations. In steady state it typically completes in 2–4. But on the first block after a reset, or with extreme parameter combinations (speed=2 × stretch=2 × pitch=±12), more iterations may be needed, each calling `fillInputScratch` + SoundTouch processing. This creates worst-case CPU spikes.

**Recommendation C:** Pre-compute the total input frames needed before the loop using the effective ratio, and feed it all in one `fillInputScratch` + `putSamples` call. Then drain in a single `receiveSamples` call. This converts the variable-iteration loop into a fixed two-step: feed-all, drain-all.

---

## 3. Effects Chain Performance Analysis

### 3.1 Chain Length & Ordering

The per-buffer effects chain in `ChannelStrip::processDSP` processes **in series**, per buffer, per block:

1. **Ducking** — per-sample envelope follower (pre-FX gain calculation)
2. **Delay** — circular buffer with multi-tap, fractional reads, wow/flutter modulation, ping-pong, saturation (`std::tanh`), high-cut IIR filter — all per-sample
3. **High-pass filter** — IIR per-sample
4. **Low-pass filter** — IIR per-sample  
5. **Tremolo** — sine LFO per-sample with branching
6. **Chorus** — modulated delay line with sine LFO, fractional reads, per-sample
7. **Auto-pan** — sine LFO with constant-power calculation (`sin`/`cos`), per-sample
8. **Volume ramp** — block-level gain (efficient)
9. **Limiter** (pre-reverb) — JUCE DSP limiter
10. **Reverb** — pre-delay circular buffer (per-sample) → JUCE reverb → wet/dry mix (per-sample) → ducking (per-sample)
11. **Limiter** (post-reverb)

### 3.2 Chain Cost Assessment

**Worst case with all effects enabled on all 8 buffers:**

For a 512-sample stereo block at 44.1 kHz:
- 8 buffers × 512 samples × 2 channels = 8,192 sample operations per effect stage
- With ~10 active stages (each doing per-sample work with transcendental functions like `sin`, `cos`, `tanh`): **~80,000+ sample-level operations per block**
- Plus 8 × SoundTouch processing (overlap-add FFT-based)
- Plus 8 × 2 limiter passes (JUCE DSP limiter does look-ahead)

This is a **significant** workload. At 128-sample blocks (common for live use), the deadline is ~2.9ms at 44.1 kHz. The effects chain alone could consume a substantial fraction of that budget.

**Recommendation 3A: Effect bypass short-circuits.** The current code checks `effects().delayEnabled` etc. at the top of each stage, which is good. However, the per-sample inner loops still execute branching *within* the sample loop — e.g., `if (effects().tremoloEnabled && params.tremoloDepth > 0.0f)` inside the HPF/LPF/tremolo combined loop. **Split these into separate loops** to allow the compiler to vectorize each independently.

**Recommendation 3B: Consolidate filter+FX passes into a `juce::dsp::ProcessorChain`.** The current implementation uses manual per-sample loops. A `ProcessorChain<IIR::Filter, IIR::Filter, ...>` would use JUCE's optimized block-processing paths and allow SIMD auto-vectorization.

**Recommendation 3C: Consider reducing simultaneous active effects.** When a modifier system can stack delay + reverb + chorus + tremolo + auto-pan + filters on all 8 buffers simultaneously, the worst case is catastrophic for CPU. Options:
- **Effect budget system:** Limit the total number of active effects across all buffers (e.g., max 16 effect instances total).
- **Effect priority:** When budget exceeded, auto-disable the least important effect.
- **Lazy processing:** Skip effects on buffers that aren't playing.

**Recommendation 3D: Move `std::tanh` saturation to a polynomial approximation.** The delay feedback drive uses `std::tanh(fbSample * drive)` per-sample per-channel. A 3rd-order rational approximation (`x * (27 + x*x) / (27 + 9*x*x)`) is ~5x faster and audibly indistinguishable.

**Recommendation 3E: The limiter runs twice per block (pre-reverb and post-reverb).** The post-reverb limiter is necessary, but the pre-reverb limiter may be redundant if input levels are controlled. Consider making the pre-reverb limiter conditional on reverb being enabled, and removing it entirely when reverb is off.

---

## 4. Sample Rate Handling

### 4.1 No Restriction on Host Sample Rate

The plugin accepts any sample rate the host provides. There is no validation or restriction in `prepareToPlay`. All internal processing scales linearly (or worse) with sample rate:
- SoundTouch's overlap-add window sizes are in milliseconds, so the number of samples processed per window increases
- All per-sample loops execute proportionally more iterations
- Scratch buffer sizes and memory usage scale proportionally

At 96 kHz, the plugin does **2x the work** per block with the same deadline. At 192 kHz, **4x**.

**Recommendation 4A: Internally resample to 44.1 kHz.** The plugin plays back pre-recorded audio samples (not processing live input). There is no benefit to running the entire engine at 96 or 192 kHz. Instead:
- Accept the host sample rate in `prepareToPlay`
- Resample loaded audio to 44.1 kHz (or 48 kHz) at load time
- Run all internal processing (SoundTouch, effects) at the internal rate
- Resample output back to host rate in the final stage

This would roughly halve CPU at 96 kHz and quarter it at 192 kHz, with negligible quality loss for the use case.

**Recommendation 4B: As a simpler alternative, validate and warn.** If internal resampling is too complex, at minimum document supported sample rates and emit a diagnostic when running above 48 kHz.

### 4.2 File Sample Rate Mismatch

When loaded audio files have a different sample rate than the host, the code handles this via per-sample interpolation with the ratio `fileSampleRate / hostSampleRate` in the playhead advancement step. This works correctly but:
- The `canBulkCopy` fast path requires `fileSR == hostSR`, so any mismatch forces the slow per-sample path
- A 48 kHz file in a 44.1 kHz session (or vice versa) is extremely common and always takes the slow path

**Recommendation 4C: Resample audio to host sample rate at load time.** This eliminates the continuous per-sample ratio compensation, enables the bulk-copy fast path, and simplifies SoundTouch configuration (which also needs to know the correct sample rate for its overlap windows).

---

## 5. Threading & Lock Contention

### 5.1 SpinLock on Audio Data Access

`AudioBuffer::getAudioDataSnapshot()` acquires a `juce::SpinLock` every call. This function is called:
- At the start of `processBlock` (hasAudioLoaded check)
- In `processWithRepitching` / `processWithTimeStretch`
- In `getDurationInSamples`, `getPlayheadPositionInSeconds`, `getFileSampleRate`, `getLoadedFileName`

On the audio thread, the SpinLock is typically uncontended. But:
- If the UI thread calls `getPlayheadPositionInSeconds()` or similar while the audio thread is in `processBlock`, the audio thread may spin-wait
- `setLoadedAudioData()` acquires the same lock from whatever thread calls it (background loader via `applyPendingLoads` on the audio thread — this is fine since it's the same thread)

**Recommendation 5A: Use `ReferenceCountedObjectPtr` with atomic load/store instead of SpinLock.** Since `LoadedAudioData` is already reference-counted, a single `std::atomic<LoadedAudioData*>` with acquire/release semantics could replace the SpinLock entirely. The audio thread would do a single atomic load at the top of `processBlock` and hold the `Ptr` for the duration — no locking needed for subsequent accesses.

### 5.2 Atomic Traffic in Inner Loops

The per-sample loops in `fillInputScratch` and `processWithRepitching` load multiple atomics per sample:
- `playheadPosition` (written every sample in the repitch path)
- `loopWindowEnabled`, `loopStartSamples`, `loopEndSamples`
- `slicingModeActive`, `sliceTriggered`, `pingPongEnabled`, `pingPongPeriodSamples`, `pingPongPhasePosition`, `pingPongGoingForward`

Atomic loads with sequential consistency are not free — on x86 they introduce memory fences that prevent out-of-order execution. On ARM (Apple Silicon), they're even more expensive.

**Recommendation 5B:** Snapshot ALL atomic state into local variables at the top of each processing function. Only write `playheadPosition` once at the end. The `fillInputScratch` lambda partially does this (local `currentPos`) but still loads many atomics per-sample for slice/loop/ping-pong checks. Convert these to local booleans loaded once.

### 5.3 `std::function` Callback on Audio Thread

`AudioBufferManager::perBufferProcessor` is a `std::function` invoked for every buffer every block. `std::function` involves a virtual call through a type-erased wrapper. While not catastrophic, a raw function pointer or template would avoid the indirection.

**Recommendation 5C:** Replace `std::function` with a simple interface pointer or `std::function` that's checked once outside the loop and stored as a local.

---

## 6. Memory Allocation on the Audio Thread

### 6.1 Scratch Buffer Resizing

Several buffers use `setSize(..., true)` with `keepExistingContent=true`:
- `stretchInScratch`, `stretchOutScratch`, `stretchInterleavedIn`, `stretchInterleavedOut` in `processWithTimeStretch`
- `tempBuffer` in `AudioBufferManager::processBlock`
- `scratchBuffer` in `PluginProcessor::processBlock`
- `previousBlockBuffer` in `AudioBuffer::processBlock`

`juce::AudioBuffer::setSize` with growing dimensions calls `malloc` on the realtime thread. While these typically stabilize after the first few blocks, any parameter change that increases the effective ratio can trigger a resize.

**Recommendation 6A:** Pre-allocate all scratch buffers in `prepare()` with worst-case sizes. Use the maximum possible `totalTempoRatioForIO` (speed=4× stretch=4× pitch=±24 semitones) to size buffers that will never need to grow during processing.

### 6.2 `std::vector` in SoundTouch Feed Loop

`processWithTimeStretch` declares `std::vector<float*> outPtrs` and `std::vector<const float*> inPtrs` inside the processing function. These allocate on every block.

**Recommendation 6B:** Replace with stack-allocated `std::array<float*, 2>` (stereo is the maximum channel count based on the bus layout).

### 6.3 `duckGains` Vector

`ChannelStrip::processDSP` resizes `std::vector<float> duckGains` per block via `resize()`. If the block size is stable, this is a no-op, but if it varies (which some hosts do), it allocates.

**Recommendation 6C:** Pre-allocate `duckGains` in `prepareDSP` or use a stack-allocated array.

---

## 7. Repitch Path Per-Sample Overhead

The `processWithRepitching` method uses a **sample-by-sample** loop with:
- Per-sample `speedSmoother.getNextValue()` (SmoothedValue — relatively cheap)
- Per-sample `handleSlicePlayback` (multiple atomic loads + branching)
- Per-sample loop-window boundary checks
- Per-sample ping-pong checks
- Per-sample linear interpolation (2 reads + lerp per channel)
- Per-sample `outputBuffer.setSample` (bounds check in debug)
- Per-sample crossfade application
- Per-sample playhead store (`playheadPosition.store`)

For 8 buffers × 512 samples this is 4,096 iterations of a non-trivial inner loop — **before** effects processing.

**Recommendation 7A: Rewrite as a block-based processor.** Process in chunks between boundary events (slice trigger, loop wrap, ping-pong direction change). Between boundaries, the playhead advances linearly and the work can be vectorized.

**Recommendation 7B:** Use `juce::LagrangeInterpolator` (or `CatmullRomInterpolator`) for higher-quality interpolation with block-processing support, replacing the manual per-sample linear lerp.

---

## 8. Debug Build Performance

### 8.1 Tearing Debug Analysis (JUCE_DEBUG only)

The `#if JUCE_DEBUG` block at the end of `processBlock` iterates over every sample of every channel to:
- Check for NaN/Inf
- Check for clipping
- Multi-level discontinuity detection (3 thresholds)
- Zero-run counting
- RMS calculation (full-block sum)
- DC offset calculation (full-block sum)
- RMS jump comparison
- DC drift comparison

This is essentially a second full pass over the output data. In combination with the already-expensive processing, this makes debug builds **2x+ slower** in the audio path, virtually guaranteeing dropouts during interactive testing.

**Recommendation 8A:** Make tearing debug analysis run on a decimated schedule (e.g., every 10th block) or on a separate thread via a lock-free queue of output snapshots.

**Recommendation 8B:** Move NaN/Inf checks to a simple `juce::FloatVectorOperations::findMinAndMax` check (if min < -2 || max > 2 || isnan), which can be SIMD-optimized.

---

## 9. Architectural Concerns

### 9.1 Serial Processing of 8 Buffers

`PluginProcessor::processBlock` processes all 8 buffers sequentially:
```
for bufferIndex in 0..7:
    scratchBuffer.clear()
    processSingleBuffer(bufferIndex, scratchBuffer)   // SoundTouch + effects
    mix into output
```

Each `processSingleBuffer` call includes the full SoundTouch pipeline + full effects chain. There's no parallelism.

**Recommendation 9A: Consider double-buffered parallel processing.** With 8 independent buffers, the work is embarrassingly parallel. Using `juce::ThreadPool` or a work-stealing approach, 2–4 buffers could be processed simultaneously on different cores, with results merged after all complete. This would provide near-linear speedup on multi-core systems.

**Caveat:** This requires careful memory management (no shared scratch buffers) and the output merge must be deterministic. The benefits are significant though — it's the single highest-impact optimization possible.

### 9.2 Redundant Processing of Non-Playing Buffers

`AudioBufferManager::processBlock` iterates all 8 buffers and checks `hasAudioLoaded()`. Each `hasAudioLoaded()` acquires the SpinLock. Even for empty/non-playing buffers, the function call overhead is non-zero.

**Recommendation 9B:** Maintain a bitfield of loaded/playing buffers updated at load/play/stop time. Skip unloaded buffers without function calls.

### 9.3 `playAll()` Called Every Block

When the host transport is playing and playback is enabled, `app.bufferManager.playAll()` is called **on every single audio block** (line ~690 of PluginProcessor.cpp). This iterates all 8 buffers and calls `play()` on each, which includes debug-mode tearing stats reset logic and state checks.

**Recommendation 9C:** Track whether buffers are already playing and skip re-triggering. Alternatively, make `play()` a no-op if already playing (it partially does this via `params.isPlaying` but the function still executes).

---

## 10. Specific Tearing Risk Areas

### 10.1 Mode Transition Crossfade Timing

The repitch↔stretch crossfade length is dynamically computed based on speed/stretch/pitch parameters. At extreme combinations (speed=4, stretch=4, pitch=±24), the crossfade can extend to 80ms (3,528 samples at 44.1 kHz). This is a substantial portion of a block and may itself cause audible artifacts if the two paths produce very different audio content.

**Recommendation 10A:** Consider a fixed 10–20ms crossfade with a preceding fade-out of the old path and fade-in of the new path (rather than blending both at full volume). The current equal-power blend assumes both paths produce similar content, which isn't true during mode transitions.

### 10.2 SoundTouch Priming Underruns

Despite the T2 priming optimization, the first block after a SoundTouch reset can still underrun because:
- The priming amount is estimated from `getLatencySamples()`, which may not account for all internal buffering
- At high ratios, SoundTouch may consume more input than estimated before producing output
- The 24-iteration safety cap can be hit, leaving the block partially empty

**Recommendation 10B:** Over-prime by 2x the estimated amount. The extra CPU cost is one-time (per reset) and eliminates the audible crackle.

### 10.3 Crossfade During Stretch Mode

The source-level crossfade (`applyCrossfadeToSliceTransition`) runs on the **input to SoundTouch**, not on its output. SoundTouch's overlap-add algorithm then processes the crossfaded input and may introduce additional artifacts at window boundaries. The interaction between the source crossfade and SoundTouch's internal overlap-add is non-deterministic.

**Recommendation 10C:** When SoundTouch is active, implement crossfading on the **output** side of SoundTouch rather than the input side. This means maintaining a brief ring buffer of SoundTouch output and fading between old and new output around transition points.

---

## 11. Recommendations Summary (Prioritized)

### Critical (Do First)
1. **Pre-allocate all scratch buffers** at worst-case sizes in `prepare()` (§6.1, §6.2, §6.3)
2. **Replace SpinLock with atomic RCO pointer** for audio data access (§5.1)
3. **Snapshot all atomics once** at top of processing, not per-sample (§5.2)
4. **Replace `std::vector` with `std::array`** in SoundTouch feed loop (§6.2)

### High Impact
5. **Extend bulk-copy fast path** to handle speed≠1.0 cases with block resampling (§2.2A)
6. **Resample loaded audio to host SR at load time** to eliminate continuous per-sample ratio math (§4.2)
7. **Split combined filter/tremolo per-sample loop** into separate loops for vectorization (§3.2A)
8. **Replace `std::tanh` with polynomial approximation** in delay feedback (§3.2D)
9. **Effect budget system** to cap maximum simultaneous active effects (§3.2C)

### Medium Impact
10. **Internal 44.1 kHz processing** with output resampling for high-SR hosts (§4.1)
11. **Parallel buffer processing** using worker threads (§9.1)
12. **Block-based repitch processing** with vectorized interpolation (§7)
13. **Output-side crossfading** when SoundTouch is active (§10.3)
14. **Over-prime SoundTouch by 2x** estimated amount (§10.2)

### Low Impact / Quality of Life
15. **Reduce tearing debug overhead** with decimated or off-thread analysis (§8)
16. **Make `playAll()` a no-op** when already playing (§9.3)
17. **Conditional pre-reverb limiter** (§3.2E)
18. **Consider `juce::dsp::ProcessorChain`** for effects (§3.2B)

---

## 12. Estimated Impact

| Optimization | Est. CPU Reduction | Complexity |
|---|---|---|
| Pre-allocate scratch buffers | 1-3% (eliminates spikes) | Low |
| Atomic snapshot (not per-sample) | 5-10% | Low |
| Bulk-copy fast path extension | 10-20% (for non-stretch cases) | Medium |
| Resample audio to host SR at load | 5-15% (eliminates ratio math) | Medium |
| Internal 44.1k processing | 50% at 96k, 75% at 192k | High |
| Parallel buffer processing | 50-75% on multi-core | High |
| Effect chain vectorization | 10-20% | Medium |
| `tanh` approximation | 1-2% | Low |
| Effect budget system | Prevents worst-case blowup | Medium |
| Output-side SoundTouch crossfade | Quality improvement (not CPU) | Medium-High |

---

## Appendix: Current Processing Cost Per Audio Block (Worst Case)

Assuming 8 loaded buffers, all playing, all with SoundTouch active (stretch + pitch), all effects enabled, 512-sample block at 44.1 kHz:

| Stage | Operations | Est. Cycles |
|---|---|---|
| 8× `fillInputScratch` (per-sample, high ratio) | 8 × ~2048 × 20 ops | ~320K |
| 8× SoundTouch interleave | 8 × 2048 × 2 | ~32K |
| 8× SoundTouch process | 8 × overlap-add | ~400K |
| 8× SoundTouch deinterleave | 8 × 512 × 2 | ~8K |
| 8× Delay (per-sample, multi-tap, wow/flutter) | 8 × 512 × 2 × ~30 ops | ~245K |
| 8× HPF+LPF+tremolo | 8 × 512 × 2 × ~10 ops | ~82K |
| 8× Chorus | 8 × 512 × 2 × ~15 ops | ~123K |
| 8× Auto-pan | 8 × 512 × 2 × ~12 ops | ~98K |
| 8× Volume ramp | 8 × 1 (block gain) | ~0.1K |
| 8× Limiter (×2) | 8 × 2 | ~32K |
| 8× Reverb (pre-delay + JUCE reverb) | 8 × 512 × 2 × ~20 ops | ~164K |
| 8× Ducking (per-sample) | 8 × 512 × ~8 ops | ~33K |
| Mode transition crossfade | ~3.5K samples | ~7K |
| Tearing debug (DEBUG only) | 8 × 512 × 2 × ~15 ops | ~123K |
| **Total** | | **~1.67M cycles** |

At 3 GHz (single core), a 512-sample block at 44.1 kHz has a deadline of ~34.8M cycles (11.6ms). The worst case above (~1.67M) leaves headroom... but this is an optimistic estimate. Cache misses, branch mispredictions, and SoundTouch's internal FFT work could easily 3-5x the actual cycle count, bringing real-world usage to 5-8M cycles — still within budget but with little margin at small block sizes.

At **128 samples** (2.9ms deadline, ~8.7M cycles at 3 GHz), the margin evaporates.
