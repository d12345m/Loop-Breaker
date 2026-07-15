# Plan: Click/Pop Remediation for Stacked Modifiers (LoopBreaker)

## Context

Residual clicks occur when modifiers stack (speed + stretch/pitch via SoundTouch + rapid slice jumps).
Existing mitigations: input-side crossfades, §12 lookahead pre-priming, §10.3 output ring crossfade,
mode-transition crossfades, T8 reset crossfade, stretch fade-in, ChannelStrip declick fade-in.
User chose: structural fixes + swan-ramp de-clicker safety net + automated test harness.

## Confirmed root-cause bugs found in code review

### BUG 1 — Repitch crossfade "prev position" restarts every chunk

`AudioBuffer.cpp` `applyCrossfadeToSliceTransition()` (~L2643):
`prevPos = previousSlicePlayheadPos + sample * effectiveSpeed * srRatio` — uses chunk-local `sample`,
NOT `crossfadePosition + sample`. When a crossfade spans multiple chunks/blocks, the fade-out source
audio restarts from the original position each chunk → stutter/click. (fadeProgress correctly uses
crossfadePosition + sample; the read position does not.)

### BUG 2 — crossfadeLengthSamples mutated mid-flight across blocks

`processWithTimeStretch` temporarily raises the member `crossfadeLengthSamples` to ≥50ms and restores
it at function end. §12 lookahead-correct path halves it mid-block. In-flight crossfades that span
block boundaries recompute progress against a DIFFERENT length next block → fade gain jumps.
Fix: capture length into a per-crossfade `activeCrossfadeLen` at crossfade start; never mutate member.

### BUG 3 — No crossfade on file-boundary wrap in repitch mode

`processWithRepitching` file boundary (~L710-716): `currentPos = 0.0` (or end) with no crossfade,
while stretch path uses `startBoundaryCrossfadeLocal`. Known gap (DECLICK_REPORT §1.3).

### BUG 4 — No crossfade on plain slice-loop wrap

Both paths: non-random slicing "else" branch sets `currentPos = sliceStart` (loop within slice)
without starting a crossfade.

### BUG 5 — Lookahead 50% blend → post-jump crossfade restarts at 0%

fillInputScratch: lookahead ramps blend 0→50% of next slice by boundary; then post-jump crossfade
starts at fadeIn=0 (100% OLD slice) → instantaneous jump from 50/50 blend back to 100% old audio at
the boundary sample. Halving the crossfade length doesn't fix the restart. Fix: when prediction is
correct, start post-jump crossfade with fadeProgress preloaded to 0.5 (equal-power continuity),
or let lookahead run 0→100% and skip post-jump fade entirely.

### BUG 6 — ChannelStrip declick fade-in is a drop-to-silence

`requestDeclickFadeIn(512)` (ChannelStrip.h L567 + AppState.h ~L1490): gain starts ≈0 at the block
boundary after params applied → instant amplitude drop = click of full signal magnitude, then fade-up.
Fix: fade-down→apply→fade-up sequence (deferred param apply), or crossfade cached pre-change tail.

### BUG 7 — Mode-transition / T8 reset crossfades truncated to one block

`processBlock` transition crossfade and T8 reset crossfade compute crossfadeMs up to 80ms but clamp
`fadeSamples = min(curSamples, prevSamples, …)`. At 64/128-sample host buffers the crossfade can't
fit → residual click at small block sizes. Fix: multi-block crossfade state (position/total persisted,
old audio sourced from §10.3 stretchOutputRing snapshot which already holds ≥100ms).

### BUG 8 — Underfill tail → next block discontinuity

Underfill path fades tail to zero (2ms), but the NEXT block starts at full amplitude with no fade-in
→ boundary jump. The de-clicker (safety net) will catch this; optionally set a short fade-in flag.

### Secondary issues

- `params.speed` is a plain double written by message thread (setSpeed) and audio thread (ping-pong);
  StretchSnapshot reads it unsynchronized. Make it atomic.
- Delay time changes (delayTapTimesMs / params.delayTimeMs) are instantaneous → clicks; delayBuffer /
  chorus buffer setSize on audio thread when channel count changes (alloc + clear → dropout).
- S&H filters intentionally jump coefficients; high Q jumps can pop — consider 1-2ms coefficient ramp.

## Implementation Phases

### Phase 1 — Crossfade correctness fixes (root causes) [independent, do first]

1.1 Fix BUG 1: use `(crossfadePosition + sample)` for prev-position in applyCrossfadeToSliceTransition.
1.2 Fix BUG 2: introduce `activeCrossfadeLen` captured at each crossfade start (all sites that set
`isInCrossfade = true`); remove save/restore mutation hack in processWithTimeStretch; lookahead
halving applies to the new crossfade's captured length only.
1.3 Fix BUG 3: add boundary crossfade on repitch file wrap (mirror loop-window pattern at L731-757).
1.4 Fix BUG 4: add crossfade on slice-loop wrap in both stretch (handleSlicePlaybackLocal else-branch)
and repitch paths.
1.5 Fix BUG 5: preload post-jump crossfade progress to 50% when lookaheadPredictionCorrect
(fadeProgress starts at 0.5; keep length halving so duration matches remaining blend).

### Phase 2 — Multi-block transition crossfades (BUG 7) [depends on 1.2]

2.1 Add persistent transition-crossfade state {active, pos, totalLen} + use previousBlockBuffer AND
stretchOutputRing snapshot for old-audio continuation across blocks in processBlock and T8 path.

### Phase 3 — FX-chain (ChannelStrip) parameter-change smoothing [parallel with Phase 1/2]

3.1 Replace declick drop-to-silence (BUG 6) with fade-down→apply→fade-up: queue pending FxParams
snapshot; processDSP fades down ~1.5ms at start, swaps params, fades up ~10ms (raised cosine).
3.2 Smooth delay time changes: crossfade delay read taps over ~30ms when tap set changes.
3.3 Move delay/chorus/predelay buffer channel-count reallocation guards to prepareDSP.
3.4 Make params.speed atomic (or add atomic mirror used by snapshot).

### Phase 4 — Swan-ramp de-clicker safety net (DECLICK_REPORT Phase 1+2)

4.1 `applyDeClick()` in AudioBuffer: block-boundary adaptive raised-cosine micro-ramp
(threshold 0.05, ramp 8-64 samples). Insert after mode-transition crossfade, before
previousBlockBuffer cache. State: deClickLastSample[2], reset in prepare()/resetToDefaults().
4.2 Optional intra-block Hann-dip scan behind `deClickIntraBlockEnabled=false` default.
4.3 Keep tearing debug BEFORE de-click for honest diagnostics (or add post-declick counters).

### Phase 5 — Automated click-detection harness

5.1 New test file (pattern: ModifierSchedulerTests.cpp) rendering synthetic sine content offline
through AudioBuffer::processBlock with scripted worst-case sequences:
speed×stretch×pitch×reverse×slicing×pingpong, block sizes 64-1024, SR 44.1/48k.
Count discontinuities via TearingDebug::getDiscontinuityLevel; assert Major==0, Medium below budget.
5.2 Run before/after to quantify each phase's improvement.

## Implementation Progress (2026-07-15)

- DONE Phase 1 (BUGs 1-5): prev-position uses crossfadePosition+sample; activeCrossfadeLen captured at
  every isInCrossfade=true site (+crossfadeIsLookaheadContinuation flag); repitch file-wrap + slice-loop
  wrap crossfades added in both paths; lookahead now HOLDS at 50% (clamped progress) instead of snapping
  back, boundary consumes state, post-jump fade continues linearly 0.5→1.0 with half length, and playhead
  continues from lookaheadNextSliceReadPos (BUG 5c). Repitch crossfade prev-pos now clamps at file edges.
- DONE BUG 7: unified multi-block transition fade — new members transitionOldBuffer/FadePos/FadeLen/Active
  - stretcherResetThisBlock; beginTransitionFade()/applyTransitionFade() in AudioBuffer.cpp (~L2865, after
    applyStretchOutputCrossfade). processBlock now: trigger check (transitioned || resetOccurred) →
    beginTransitionFade → applyTransitionFade → writeToStretchOutputRing (ring now fed in BOTH modes, once,
    at end of processBlock; removed ring write in processWithTimeStretch). Old T8 single-block blend in
    processWithTimeStretch replaced by stretcherResetThisBlock=true signal. smoothedPitchActive removed
    (unused). Ring invalidated + fade cancelled on silence/new-audio-load/resetToDefaults.
- DONE BUG 6: ChannelStrip declick replaced fade-from-silence with boundary-step correction:
  processDSP → processDSPChain + applyBoundaryDeclick (measures step declickLastOut vs first sample,
  subtracts raised-cosine-decaying offset). New members declickStep[2]/declickLastOut[2]; reset in
  prepareDSP + reset(). requestDeclickFadeIn keeps same API (AppState call sites unchanged).
- DONE atomic speed: atomicSpeed mirror in AudioBuffer.h; getEffectiveSpeed/getSpeed/takeStretchSnapshot
  read mirror; writers updated: setSpeed, ping-pong flips (both paths), setPingPongMode, setParams,
  resetToDefaults, applyCrossfadeToSliceTransition direction read.
- DONE Phase 4.1 (2026-07-15): AudioBuffer block-boundary adaptive raised-cosine de-clicker uses an
  offset correction to preserve waveform continuity; it runs after transition fades and before the output
  ring/cache, and its state resets on prepare, reset, and silent output.
- DONE Phase 3.2 (2026-07-15): ChannelStrip delay reads use a 30ms equal-power crossfade when the base
  delay time or multi-tap layout changes. Tap sets are fixed-size arrays, avoiding per-block allocation.
- DONE Phase 5.1 (2026-07-15): ClickDetectionTests.cpp renders deterministic stereo sine content through
  speed, stretch, pitch, reverse, slicing, and ping-pong transitions at 64–1024 sample blocks and
  44.1/48 kHz. It asserts no invalid or major discontinuities and caps medium discontinuities.
  The test is registered in both BufferTest.jucer and the generated Xcode project.
- DONE Phase 5.2 infrastructure (2026-07-15): CMake target `LoopBreakerClickTests` is a standalone
  console runner for the Click Detection JUCE test category. It links the SoundTouch submodule's native
  CMake target, so it runs without loading a plugin host or duplicating SoundTouch unity sources.
- DONE Phase 3.3 (2026-07-15): ChannelStrip FX work buffers are allocated only from the host prepare
  lifecycle. AppState::prepareDSP() is called by plugin, desktop, and iOS prepareToPlay paths; the
  per-buffer audio callback now only calls processDSP(). Delay, chorus, pre-delay, reverb-wet, ducking,
  and tremolo buffers are stereo preallocated with manager headroom and processDSP asserts capacity.
- DONE Phase 4.2/4.3 (2026-07-15): optional (default off) 8–32 sample intra-block Hann-tail correction
  is available through AudioBuffer::setIntraBlockDeClickEnabled(). Tearing discontinuity counters inspect
  raw pre-declick output, deliberately favoring visibility of underlying faults over corrected output.
- DONE secondary follow-ups (2026-07-15): high-Q S&H IIR coefficient changes ramp for 15ms across DSP
  blocks; a SoundTouch underfill tail now schedules a 2ms equal-power fade-in for the following block.
- TODO next: configure/build and run `LoopBreakerClickTests`; tune the measured medium-discontinuity
  budget only if the deterministic baseline requires it.

## Verification

1. Build: task "Build All (Debug)"; run harness (5.1) — record baseline counts before Phase 1.
2. pluginval task (strictness 10).
3. Manual: TearingDebugPanel with stress preset (stretch+speed+arp slicing at 1/16), listen at 128-sample buffer.
4. A/B the de-clicker enable flag to confirm no pumping on drum transients.

## Key files

- Source/AudioBuffer.cpp / .h — Phases 1, 2, 4 (fillInputScratch, applyCrossfadeToSliceTransition,
  processBlock, processWithRepitching, processWithTimeStretch)
- Source/ChannelStrip.h — Phase 3 (processDSP head, requestDeclickFadeIn, delay block)
- Source/AppState.h ~L1490 — call site of requestDeclickFadeIn (switch to pending-params mechanism)
- Source/TimeStretchSoundTouch.h — no changes expected
- New: Source/ClickDetectionTests.cpp (or extend ModifierSchedulerTests.cpp)

## Scope exclusions

- No SoundTouch engine modifications (keep OLA settings as-is)
- No architecture rewrite of the 8 fade systems into one manager (documented as future consideration)
- No changes to granular processor or FX ordering
