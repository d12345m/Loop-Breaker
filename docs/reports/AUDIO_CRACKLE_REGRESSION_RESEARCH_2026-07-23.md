# Audio pop/click regression research

**Date:** 2026-07-23

**Branch audited:** `codex/ios-playback-app`

**HEAD audited:** `43fd95c`

**Immediate pre-warning baseline:** `a534b9e`

**Warning-cleanup commit:** `ce269bd`

**Purpose:** durable investigation and implementation handoff; this report does not contain production fixes.

## Executive conclusion

The warning-cleanup work was not purely mechanical. Commit `ce269bd` changed
SoundTouch from a drain-first loop to an unconditional feed-first loop. That new
loop has a high-confidence accounting defect: it feeds
`ceil(blockSize * effectiveRatio)` on every callback, but can remove only one
host block of output. Whenever the product is non-integral, the rounding surplus
accumulates in SoundTouch. The feed arithmetic proves a positive cumulative
schedule bias, and a direct probe corroborated it despite SoundTouch's normal
batch-size oscillation.
At 48 kHz, block size 64, and effective ratio 0.8, the current policy left
11,712 ready-output frames queued after 6,000 callbacks (about 244 ms); an
otherwise identical fractional-debt feed left 4,800 ready frames (about 100 ms).
The remaining ready-output floor reflects the deliberately large production
over-prime plus SoundTouch batching; a complete watermark controller should
allow it to settle to an explicit target.

That regression interacts with a more fundamental pre-existing problem:
most externally requested playhead changes are raw atomic stores. Switch Part,
preset recall, host restart, and the global start offset do not initiate a source
crossfade, reset/reconcile SoundTouch, invalidate resampler/lookahead state, or
coordinate with already queued audio. The full Reset modifier does clear
SoundTouch, but it destroys prior transition history and restarts without a
continuous handoff. For the raw paths, the new backlog puts new-position samples
behind old-position audio and separates the source write-head even farther from
what is currently audible.

There are also several independent, actionable click sources:

1. The SoundTouch bulk-copy fast path can enter an infinite loop when a looping
   feed crosses the file boundary and can leave a one-past-end playhead when a
   feed ends exactly at EOF. A naive recompute-only fix would then hard-splice
   the endpoints.
2. Vendored SoundTouch explicitly documents that the currently disabled
   `SOUNDTOUCH_PREVENT_CLICK_AT_RATE_CROSSOVER` option prevents clicks when rate
   or pitch crosses 1.0.
3. The multi-block "transition crossfade" replays already-emitted historical
   audio, so its first sample can itself be discontinuous.
4. The SoundTouch startup fade restarts from near zero on each small host block
   instead of progressing monotonically across blocks.
5. Variable host callback sizes cause the renderer to process the full scratch
   capacity and discard the excess, advancing the source farther than the host
   received.
6. Reverse changes live-reconfigure SoundTouch overlap storage on a render
   worker, including a `delete[]`/`new[]`, without an explicit direction
   crossfade.
7. The three background render workers are ordinary `std::thread`s with no
   real-time priority/workgroup setup. They yield and then sleep while idle,
   while the host audio thread spin-waits indefinitely for every task.
8. The wider callback still constructs parameter-ID strings, performs map/vector
   work, takes spin locks, plans modifiers, and can format debug messages.

These form two different artifact classes and need different evidence:

- **Waveform discontinuity:** adjacent computed samples are unrelated. The
  offline harness directly reproduced this for raw seeks, part changes, reverse,
  and several SoundTouch transitions.
- **Deadline miss:** the callback or a worker completes after the device
  deadline, so the host/device inserts or repeats audio. Static inspection found
  several credible causes, but the offline sample harness cannot prove this
  class. It requires end-to-end callback timing and device/host xrun capture.

A de-clicker can conceal some waveform discontinuities. It cannot repair an
audio callback that missed its deadline.

The offline render did not show that every `ce269bd` case is worse. The new loop
improved some block-size/scenario combinations and worsened others. It changed
artifact timing and severity rather than creating every underlying
discontinuity. The right response is therefore not a blind revert of the whole
warning commit. Fix the queue controller, transport-transition semantics, and
specific continuity bugs in isolated commits, with a rendered-audio regression
suite in place first.

## Confidence and priority

| ID | Priority | Confidence | Attribution | Finding |
| --- | --- | --- | --- | --- |
| F-01 | P0 | High | Introduced by `ce269bd` | Feed-first `ceil()` accounting grows the SoundTouch backlog |
| F-02 | P0 | High | Pre-existing, amplified by F-01 | Direct playhead changes bypass every continuity protocol |
| F-03 | P0 | High | Pre-existing | SoundTouch bulk loop can hang or leave a one-past-end position |
| F-04 | P1 | High | Vendored default, pre-existing | SoundTouch's documented rate-crossover click prevention is off |
| F-05 | P1 | High | Introduced by `9a47df8` | Transition fade replays old history instead of future old audio |
| F-06 | P1 | High | Pre-existing | Startup fade restarts every callback |
| F-07 | P1 | High | Pre-existing; important on AU/AUv3 | Variable callback sizes render and discard extra samples |
| F-08 | P1 | High for RT defect; medium-high audibly | Pre-existing | Reverse reconfigures/allocates SoundTouch state live without a transition |
| F-09 | P1 | High | Pre-existing | Multi-atomic transport/slice publication can tear or lose commands |
| F-10 | P1 | High if an underfill occurs | Pre-existing | Underfill fades toward zero but the following block reconnects abruptly |
| F-11 | P2 | High | Pre-existing | Slice lookahead compares incompatible sample domains |
| F-12 | P1 | High | Pre-existing/historical regression | DSP lifecycle and variable-size handling perform real-time resets/allocations |
| F-13 | P2 | High | Removed before `ce269bd` | AudioBuffer's final boundary safety net and audio click tests were removed |
| F-14 | P2 | High/conditional | Pre-existing | Several secondary transition-state bugs remain |
| F-15 | Investigation | Medium | Introduced by `ce269bd` | Separate SoundTouch target forces `-O3 -ffast-math` in every configuration |
| F-16 | P1 | High for RT defect; device causality unproven | Pre-existing | The render-worker pool is not real-time-qualified and the host thread waits without a deadline |
| F-17 | P1 | High for RT defect; device causality unproven | Pre-existing/adjacent commits | The complete callback still allocates, locks, plans, scans, and formats strings |

### Symptom-to-finding triage

| Observed symptom | First findings to test | Discriminating measurement |
| --- | --- | --- |
| Click exactly at Switch Part, running preset recall, or host restart | F-02, F-05, F-09 | Raw boundary delta, command generation, source/audible positions, ready/unprocessed depth |
| Artifact worsens over seconds/minutes with a fixed non-integral stretch/rate | F-01 | FIFO/credit slope and event-to-audible delay over equal elapsed time |
| Hang/dropout near a short loop or file boundary | F-03 | Subprocess watchdog plus final playhead invariant |
| Click when Speed/Pitch crosses effective rate 1.0 | F-04, F-14 | Macro off/on A/B, setter cadence, crossover sample |
| Strong dependence on callback size or AU/AUv3/iOS host | F-07, F-12, F-16 | Requested versus rendered frame count, callback timing, worker timing/xruns |
| Click at a forward/reverse change | F-08, F-14 | Allocation trace, direction/set-window event, raw delta |
| Repeated amplitude dip on first SoundTouch blocks | F-06 | Per-sample startup gain envelope across callback boundaries |
| Click at a Delay modifier or multi-tap layout change | F-13/F-14, F-17 | Old/new read-head trace, allocation count, raw delta |
| Audible pop with no discontinuity in the computed output file | F-16, F-17 | End-to-end device capture correlated with late callback/xrun events |

## Commit forensics

### What `ce269bd` changed in the audio path

The commit is described as a warning cleanup, but it contains three DSP-relevant
changes.

#### 1. SoundTouch feed scheduling changed

Current code unconditionally advances the source and feeds SoundTouch before it
tries to drain the pipeline:

- [`AudioBuffer.cpp:2188-2203`](../../Source/AudioBuffer.cpp#L2188)

The parent implementation drained all already-available output first. It fed
only when the current output block still had a deficit. This is the strongest
warning-cleanup regression candidate and is analyzed as F-01.

#### 2. Priming became bounded and chunked

Current priming feeds a large requested prime in chunks bounded by the
preallocated scratch capacity:

- [`AudioBuffer.cpp:2127-2165`](../../Source/AudioBuffer.cpp#L2127)

This is directionally correct for avoiding a scratch-buffer allocation on the
render thread. It feeds the same total prime as the parent; there is no evidence
that chunking itself increases the retained FIFO. Keep the chunking, but account
for the entire prime in the steady-state reserve controller so the deliberate
2x overfeed is repaid instead of remaining permanent latency.

#### 3. SoundTouch became a separate CMake target

The project now adds and links the vendored target:

- [`CMakeLists.txt:53-70`](../../CMakeLists.txt#L53)
- [`CMakeLists.txt:205-209`](../../CMakeLists.txt#L205)

On non-MSVC builds, the vendored target forces `-O3 -ffast-math`; that includes
every configuration in the audited Apple builds:

- [`ThirdParty/soundtouch/CMakeLists.txt:7-18`](../../ThirdParty/soundtouch/CMakeLists.txt#L7)

Those flags pre-existed in the vendored CMake project, but `ce269bd` made them
active in Loop Breaker's main build. Before that commit, SoundTouch `.cpp` files
were unity-included into `PluginProcessor.cpp` and inherited that translation
unit's flags. Separate compilation is a good warning-isolation design, but it
changed floating-point, optimization, and potentially splice-selection behavior
at the same time.
Treat F-15 as an A/B variable, not as the leading root cause.

### Changes in `ce269bd` that appear safe

The `std::signbit` direction comparison, approximate sample-rate comparison,
constructor parameter rename, enum exhaustiveness, index casts, and debug-only
field guards appear semantically safe. Disabling verbose tearing diagnostics by
default on iOS is also sensible because render-thread logging can itself miss
deadlines.

### Adjacent commits

- `0280da5` and `213fdfc` change planned modifier queue behavior. They can change
  which modifiers run and how frequently. They do not directly alter DSP sample
  equations at HEAD, but the scheduler itself executes inside the audio callback
  and can affect deadline behavior (F-17).
- `43fd95c` is mostly platform/UI restructuring. It does not modify
  `AudioBuffer.cpp`, but iOS/AUv3 callback-size behavior makes F-07 and F-12 more
  relevant.
- `3ae62f7`, shortly before the warning cleanup, removed the final AudioBuffer
  boundary de-clicker. If the listening baseline predates July 15, that removal
  is also part of the audible regression history.

## Controlled evidence

### Offline AudioBuffer render

A temporary Release-mode harness rendered the same smooth two-tone stereo source
through current HEAD and through an `a534b9e` AudioBuffer/header implementation
linked to the current external SoundTouch library. This synthetic hybrid
isolates the pre/post-`ce269bd` AudioBuffer bundle from the new SoundTouch
compiler flags. It is not a literal checkout-to-checkout comparison and it
changes all of `ce269bd`'s AudioBuffer edits, not only the feed hunk.

Configuration:

- 48 kHz, stereo.
- Fixed host blocks of 64, 128, 256, and 512.
- 32 warm-up blocks, followed by 420 measured blocks.
- Baseline, direct seeks, part-window changes, safe slice triggers, reverse,
  rate crossings, reset-to-beginning, ping-pong, and a stacked script.
- The analyzer concatenated callbacks and counted adjacent-sample deltas over
  0.05, 0.10, and 0.20 full scale. It also checked zero runs and NaN/Inf.
- The smooth-source baseline maximum delta was approximately 0.0071 in every
  block size. A 0.10 delta is therefore more than 14 times the fixture's normal
  maximum slope.

Prototype event schedule, preserved so the result can be reconstructed:

- Direct seeks at measured callbacks 64, 160, and 272.
- Part-window/start changes at callbacks 64 and 192.
- Slice triggers every 48 callbacks after callback 0.
- Reverse flips every 64 callbacks after callback 0.
- Rate changes at callbacks 72, 160, 248, and 336.
- Raw restarts at callbacks 80, 224, and 336.
- Stacked script: slice at 32; enter Stretch 0.5/Pitch +7 at 72; slice at 136;
  Speed -1.5 at 184; enable ping-pong at 248; slice at 312; disable ping-pong
  and return Speed/Pitch/Stretch to unity at 376.

The source was deterministic, 12 seconds long, and used two smooth tones per
channel (left 137/223 Hz, right 173/257 Hz) at summed peaks below 0.28. No RNG
or seed was involved.

Representative 128-sample result:

| Scenario | Current `>.10` / max delta | Parent `>.10` / max delta | Interpretation |
| --- | ---: | ---: | --- |
| Baseline repitch | 0 / 0.007 | 0 / 0.007 | Analyzer control is clean |
| Direct seek, repitch | 6 / 0.340 | 6 / 0.340 | Structural raw-seek bug predates cleanup |
| Direct seek, SoundTouch | 4 / 0.259 | 10 / 0.321 | Still broken; current queue changes timing |
| Part switch, SoundTouch | 14 / 0.264 | 5 / 0.185 | Clear regression at this block size |
| Safe `triggerSlice`, SoundTouch | 0 / 0.013 | 0 / 0.013 | Existing slice crossfade is an important clean control |
| Reverse, SoundTouch | 2 / 0.180 | 2 / 0.129 | Same count, worse peak |
| Rate crossover | 10 / 0.304 | 28 / 0.511 | Both click; current happens to be better here |
| Raw `resetToBeginning()`, SoundTouch | 2 / 0.121 | 2 / 0.102 | Both expose the unprotected raw restart path |
| Ping-pong, this fixture | 0 / 0.011 | 0 / 0.011 | No click reproduced in this specific case |
| Stacked script | 4 / 0.262 | 3 / 0.262 | Both contain severe deltas |

Results by block size for the most relevant SoundTouch cases are below. Each
cell is `count above 0.10 / maximum delta`.

| Block | Direct seek current / parent | Part switch current / parent | Reverse current / parent | Rate crossing current / parent | Raw restart current / parent |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 64 | 8 / 0.280 · 10 / 0.276 | 7 / 0.191 · 9 / 0.213 | 3 / 0.224 · 3 / 0.181 | 14 / 0.375 · 14 / 0.427 | 2 / 0.242 · 8 / 0.148 |
| 128 | 4 / 0.259 · 10 / 0.321 | 14 / 0.264 · 5 / 0.185 | 2 / 0.180 · 2 / 0.129 | 10 / 0.304 · 28 / 0.511 | 2 / 0.121 · 2 / 0.102 |
| 256 | 12 / 0.291 · 13 / 0.179 | 6 / 0.265 · 9 / 0.214 | 2 / 0.132 · 3 / 0.126 | 25 / 0.332 · 19 / 0.342 | 18 / 0.270 · 14 / 0.218 |
| 512 | 15 / 0.325 · 26 / 0.281 | 0 / 0.044 · 0 / 0.060 | 4 / 0.212 · 0 / 0.096 | 18 / 0.497 · 9 / 0.256 | 20 / 0.192 · 18 / 0.241 |

Important conclusions:

- Direct repitch seeks produced maximum deltas of 0.340-0.491, identical on both
  sides of `ce269bd`. That validates the suspected playhead-jump problem as a
  pre-existing structural defect.
- The `ce269bd` AudioBuffer bundle is not uniformly better or worse. It worsens
  part switching at 128, direct-seek severity at 256, and reverse/rate crossing
  at 512, while improving other cases. FIFO scheduling is the largest semantic
  hunk and can explain timing changes, but a feed-hunk-only A/B is still required
  for causal attribution.
- The existing `triggerSlice()` path remained clean in the most important
  comparisons. Do not replace that source crossfade casually; use it as the
  reference behavior for externally requested seeks.

Limitations:

- This was a desktop Release synthetic test, not an iOS device capture.
- It did not include the full multi-pad FX/mixer path.
- Adjacent-sample thresholds are strong evidence on a smooth fixture, but they
  are not a complete perceptual model for drums or noise.
- The parent comparison deliberately used the current SoundTouch library, so it
  does not test `-ffast-math`.
- It is an AudioBuffer-bundle comparison, not a feed-only experiment.
- Events were applied immediately before each render call, so the harness proves
  continuity failures but does not exercise message-thread/worker overwrite
  races.
- Events were scheduled by callback number and each run used 420 callbacks, so
  physical event spacing and duration scale with block size. Current/parent
  comparisons within one row are controlled; counts must not be compared across
  block-size rows as if time or event rates were normalized. The permanent test
  must schedule in absolute samples and render equal durations.

### Direct SoundTouch backlog probe

A separate probe used the vendored SoundTouch class directly with Loop Breaker's
window settings. It applied the production 2x-or-greater priming formula, then
simulated 6,000 callbacks. The current policy fed `ceil(N * ratio)` on every
callback. For isolation, the ratio was applied through `setRate()` with tempo
and pitch at unity; combined tempo/pitch/rate cases remain in the validation
matrix. The comparison carried fractional input debt between callbacks. Both
versions included the production-style bounded recovery loop, delivered every
requested host frame, used the same initial prime, and needed zero recovery
feeds in these constant-ratio cases.

| Ratio | Block | Run duration | Scheduled bias / rate | Current midpoint → final | Current ready range | Fractional midpoint → final | Fractional ready range |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0.8 | 64 | 8 s | +6,000 / 750 frames/s | 7,808 → 11,712 | 4,800-12,992 | 5,504 → 4,800 | 4,736-6,976 |
| 0.8 | 128 | 16 s | +4,500 / 281.25 frames/s | 6,976 → 10,176 | 4,800-11,456 | 6,976 → 5,568 | 4,800-6,976 |
| 0.8 | 256 | 32 s | +1,500 / 46.9 frames/s | 7,616 → 7,104 | 4,800-8,384 | 5,312 → 4,800 | 4,800-6,848 |
| 0.8 | 512 | 64 s | +3,000 / 46.9 frames/s | 6,592 → 7,872 | 4,800-9,920 | 6,592 → 5,568 | 4,800-6,848 |
| 1.3 | 64 | 8 s | +3,692.3 / 461.5 frames/s | 7,737 → 10,755 | 5,532-10,947 | 5,965 → 7,210 | 5,507-7,274 |
| 1.3 | 128 | 16 s | +2,769.2 / 173.1 frames/s | 7,082 → 9,574 | 5,537-10,017 | 7,082 → 6,029 | 5,507-7,270 |
| 1.3 | 256 | 32 s | +923.1 / 28.8 frames/s | 7,545 → 7,210 | 5,517-8,175 | 5,773 → 7,210 | 5,517-7,270 |
| 1.3 | 512 | 64 s | +1,846.2 / 28.8 frames/s | 6,698 → 7,801 | 5,517-9,101 | 6,698 → 6,029 | 5,517-7,250 |
| 0.75 | 64 | 8 s | 0 / 0 | 5,504 → 4,800 | 4,736-6,976 | n/a | Integral-product control |
| 0.75 | 128 | 16 s | 0 / 0 | 6,976 → 5,568 | 4,800-6,976 | n/a | Integral-product control |
| 0.75 | 256 | 32 s | 0 / 0 | 5,312 → 4,800 | 4,800-6,848 | n/a | Integral-product control |
| 0.75 | 512 | 64 s | 0 / 0 | 6,592 → 5,568 | 4,800-6,848 | n/a | Integral-product control |

SoundTouch naturally works in batches, and the production over-prime is large,
so a nonzero `numSamples()` is not itself a defect. The mathematical scheduled
bias is the proof of the rounding defect. Ready-output depth is corroboration,
but it is batch-phase dependent: even the fractional 1.3/64 control rises from
midpoint to final. The high common baseline is a separate reason to adopt an
explicit post-prime reserve target. These are ready-output FIFO frames, not
total source-to-audible latency. Hidden/unprocessed pipeline input adds more lead
and must be measured separately.
For `N=64`, `ratio=0.8`, the current code feeds 52 input frames. Ideal generated
output is approximately `52 / 0.8 = 65` frames, while the host removes 64:
roughly one queued frame is added per callback, or about 750 frames per second.
At the end of that probe, current/fractional `numUnprocessedSamples()` values
were 3,086/3,998 input frames. A rough visible-pipeline estimate
`ready + q * unprocessed` was therefore 15,569.5 versus 9,797.5
output-equivalent frames. This estimate omits other internal stage history; it
is useful telemetry, not a claim of exact audible latency.

## Detailed findings

### F-01 — Feed-first rounding grows SoundTouch's backlog

**Evidence**

- Unconditional feed: [`AudioBuffer.cpp:2188-2203`](../../Source/AudioBuffer.cpp#L2188)
- Recovery handles only deficits: [`AudioBuffer.cpp:2205-2241`](../../Source/AudioBuffer.cpp#L2205)
- Initial over-prime: [`AudioBuffer.cpp:2123-2165`](../../Source/AudioBuffer.cpp#L2123)
- Wrapper currently exposes only ready output, although underlying SoundTouch
  also provides `numUnprocessedSamples()` and `getInputOutputSampleRatio()`:
  [`TimeStretchSoundTouch.h:79-89`](../../Source/TimeStretchSoundTouch.h#L79)
  and [`SoundTouch.h:255-271`](../../ThirdParty/soundtouch/include/SoundTouch.h#L255)
- SoundTouch FIFO growth allocates and copies:
  [`FIFOSampleBuffer.cpp:155-183`](../../ThirdParty/soundtouch/source/SoundTouch/FIFOSampleBuffer.cpp#L155)

Let `N` be host output frames, let `r` be input frames required per output frame
(`totalTempoRatioForIO`), and let `q = 1/r` be SoundTouch's expected output per
input frame (`getInputOutputSampleRatio()`). The current callback feeds:

```text
input = ceil(N * r)
approximate generated output = input / r
surplus = ceil(N * r) / r - N
```

The surplus is positive whenever `N*r` is non-integral. There is no high-water
controller to remove it. Even when the product is integral, feed-before-drain
preserves the deliberately large startup prime instead of allowing the reserve
to settle.

**Why it can click**

- New playhead material is appended behind stale old-position material.
- Control/UI playhead and source read-head run ahead of audible output.
- SoundTouch parameter changes affect a pipeline containing older material.
- Growing FIFOs periodically allocate/copy on a render worker.
- Transition event timing becomes block-size and ratio dependent.

**Required fix**

Replace the unconditional feed with a bounded reserve controller:

1. Expose/read ready-output depth, unprocessed-input depth, and SoundTouch's
   expected output-per-input ratio.
2. Maintain explicit low/high watermarks based on measured SoundTouch batch
   requirements.
3. Feed only the deficit required to provide `N + targetReserve` output.
4. Use the signed output-credit ledger to carry fractional rounding across
   callbacks; do not independently `ceil()` every block.
5. Drain exactly the requested host frames.
6. Keep bounded recovery for unexpected batch boundaries.
7. Assert/telemeter when output or total pipeline lead exceeds the high
   watermark.

A useful controller state is:

```cpp
struct StretchQueueState
{
    double estimatedOutputCreditFrames = 0.0;
    int targetOutputReserve = 0;
    int highOutputWatermark = 0;
};
```

For a stable ratio, derive ideal new input from the output deficit:

```text
desiredOutputToAdd = max(0, N + targetReserve - credit)
idealInputToFeed = desiredOutputToAdd / q
```

Choose an integer feed count, then let the signed ledger carry its fractional
over/undershoot into the next callback.

Use this single credit accumulator for both integer-rounding carry and pipeline
overfeed. Update it for every input put, every output receive,
the entire initial prime, and every recovery-margin feed. Conceptually:

```text
credit += inputFed * outputPerInputRatio
credit -= outputReceived
```

This is needed so the controller repays over-prime and recovery margins instead
of treating them as free permanent latency. It is still an estimate requiring
hysteresis/guard bands because SoundTouch retains hidden input in several
stages. At a live parameter-ratio change, old unprocessed input can be processed
under the new settings, so ratio tags alone do not preserve old semantics.
Defer the change until the reserve is low, or reset/re-prime under the transition
protocol. Reset the ledger whenever SoundTouch is cleared or prepared.

Do not simply restore drain-first and stop there. The parent loop still produced
clicks and can starve on small blocks. Use the old loop as an A/B control, then
implement a low/high-water policy.

**Acceptance**

- At ratios 0.25-4.0 and blocks 32-1024, FIFO/lead has no positive long-run
  slope after warm-up.
- Queue depth stays within a documented bound for at least ten simulated
  minutes.
- No SoundTouch FIFO allocation occurs after warm-up.
- Event-to-audible latency remains bounded and stable.

### F-02 — Raw seeks bypass continuity and can be overwritten

The public setter is only an atomic store:

- [`AudioBuffer.h:287`](../../Source/AudioBuffer.h#L287)

Current raw-seek callers include:

- Switch Part: [`AppState.h:70-100`](../../Source/AppState.h#L70), especially
  lines 89-90.
- Applying a part after load: [`AppState.h:138-153`](../../Source/AppState.h#L138).
  This normally runs before workers on freshly swapped/reset audio, so it is an
  initialization path rather than evidence of a currently audible old-to-new
  seek. It still needs coherent loop-window/position publication.
- Preset recall: [`AppState.h:468-474`](../../Source/AppState.h#L468).
- Global start offset: [`AudioBufferManager.cpp:419-425`](../../Source/AudioBufferManager.cpp#L419).
  This path is currently dormant because `setStartOffsetSamples()` has no
  callsites. If enabled, it is worse than a one-shot seek:
  [`PluginProcessor.cpp:744-748`](../../Source/PluginProcessor.cpp#L744) calls
  `playAll()` every enabled callback, and `playAll()` would reapply the offset
  every time. Make it edge-triggered as well as transition-safe.
- Host restart through `resetToBeginning()`:
  [`PluginProcessor.cpp:640-643`](../../Source/PluginProcessor.cpp#L640),
  [`AudioBufferManager.cpp:475-481`](../../Source/AudioBufferManager.cpp#L475),
  and [`AudioBuffer.cpp:1174-1181`](../../Source/AudioBuffer.cpp#L1174).

**Repitch mechanism**

The renderer loads the new position and begins output at an unrelated source
sample. The last sample of callback N and first sample of callback N+1 form a
hard step. No source crossfade is armed.

**SoundTouch mechanism**

Only the input write-head changes. Already-ready output and unprocessed input
remain. New-position material is fed after old material, with a hard
discontinuity at their splice. F-01 makes the delay and stale tail longer.

**Full Reset is a different discontinuous path**

The Reset modifier calls `ChannelStrip::reset()` and
`AudioBuffer::resetToDefaults()`:

- [`AppState.h:737-758`](../../Source/AppState.h#L737)
- [`ChannelStrip.h:815-844`](../../Source/ChannelStrip.h#L815)
- [`AudioBuffer.cpp:1183-1227`](../../Source/AudioBuffer.cpp#L1183)

That path does clear SoundTouch and its priming/lookahead/ring/transition state,
so the stale-FIFO mechanism above does not apply. It can still click because it
destroys `previousBlockValid` and all old transition history, then starts a new
pipeline without a continuous handoff. The offline "Reset" row in this report
tested raw `resetToBeginning()`, not the full Reset modifier; add a separate
Reset-modifier scenario to the permanent harness.

**Lost-command race**

Both renderers load `playheadPosition` into a local, process a block/chunk, and
store their local position back later. A message-thread seek between the load and
store can be overwritten by the worker's final store. The scheduler currently
runs after the worker join, which avoids that race for scheduler-originated
events. A running preset recall is consumed by
`AppState::modifierTriggered()` on that scheduler-owned path, while the editor
applies a preset directly only when transport is stopped:

- [`AppState.h:501-514`](../../Source/AppState.h#L501)
- [`PluginEditor.cpp:233-244`](../../Source/PluginEditor.cpp#L233)
- [`PluginEditor.cpp:977-1001`](../../Source/PluginEditor.cpp#L977)

The concrete concurrent message-thread case is the pending-parts
`callAsync()` path, which later invokes `setActivePart()` while a subsequent
callback may already be rendering:

- [`PluginEditor.cpp:637-650`](../../Source/PluginEditor.cpp#L637)

Other future UI/control callers could recreate the same race while the raw
setter remains public. Preset recall still has the continuity defect even though
its current running-transport path avoids this particular overwrite race.

**Switch Part nuance**

`setActivePart()` stores the new part start before calling `triggerRandomSlice()`.
The slice path therefore cannot capture the true old-part position as the old
side of its crossfade. It only sees the already-teleported new part start.

**Required architecture**

Remove raw position stores from public control paths. Introduce a fixed-capacity,
real-time-safe transport command mechanism consumed by the buffer's render owner
at a defined block boundary. A command should carry at least:

```cpp
enum class TransitionReason
{
    SwitchPart, PresetRecall, Reset, HostRestart, StartOffset, SliceTrigger
};

struct TransportCommand
{
    uint64_t generation;
    TransitionReason reason;
    int64_t targetSample;
    bool updateLoopWindow;
    bool loopEnabled;
    int64_t loopStart;
    int64_t loopEnd;
};
```

There are multiple possible producers (scheduler/audio control and UI), so use a
bounded MPSC design, per-producer SPSC mailboxes, or another sequence-safe design;
do not assume a single `juce::AbstractFifo` producer without proving ownership.

On acceptance, the render owner must:

1. Apply loop window and target position coherently.
2. Acknowledge the generation exactly once.
3. Capture the actual old source/audible state.
4. Invalidate block resamplers and slice lookahead.
5. Choose an explicit transition:
   - repitch: old-read-head to new-read-head source crossfade;
   - SoundTouch: a scheduled input crossfade with bounded queue latency, or
     reset/re-prime plus a correct persistent output continuity correction;
   - highest quality: prime a second SoundTouch engine and crossfade simultaneous
     future output before swapping engines.
6. Never replay already-emitted historical audio as if it were the old future
   branch.

Keep `triggerSlice()` as a clean behavioral reference. The offline test showed
that its existing source-crossfade path can remain continuous.

### F-03 — Bulk-copy file wrap can hang or leave an invalid position

The SoundTouch input fast path is:

- [`AudioBuffer.cpp:1447-1491`](../../Source/AudioBuffer.cpp#L1447)

When a looping request crosses the file end:

1. `available` is calculated as `fileLengthSamples - pos`.
2. If it is zero, looping sets `pos = 0`.
3. `available` is not recalculated.
4. `chunk` becomes zero.
5. Neither `framesCopied` nor `pos` advances, so the `while` loop can run
   forever on the render worker.

The hang occurs before file-start audio is copied and before the final playhead
store. If a request ends exactly at EOF, the loop exits and stores
`fileLengthSamples`, a one-past-end playhead. For non-looping crossings it can
also return a short fill at that invalid position.

**Safest first fix**

Disable the bulk path unless the requested input range is entirely contiguous
and strictly inside the file. Fall through to the existing event-aware
per-sample path for any wrap. Do not "fix" only the stale `available` value:
recomputing it after `pos = 0` would make this fast path hard-splice file end to
file start and would still store `startSample + framesCopied` rather than the
wrapped `pos`.

Later, optimize a wrap by splitting it at the boundary and invoking the same
crossfade state machine as the normal path. Always store the actual final
position and assert it is in range.

**Required test**

- Very short loop with deliberately mismatched endpoints.
- Stretch or pitch active, source speed 1x, equal sample rates.
- Feed sizes that end before, exactly at, and after the file boundary.
- Run the crossing case in a subprocess with a CTest timeout, or test an
  extracted bounded-copy helper. An in-process unit-test thread cannot safely
  cancel a hung render call.
- Assert playhead is always valid and no delayed output boundary exceeds the
  smooth-fixture threshold.

### F-04 — SoundTouch documents the current rate-crossover click

Vendored SoundTouch states that
`SOUNDTOUCH_PREVENT_CLICK_AT_RATE_CROSSOVER` eliminates clicks when rate or
pitch crosses 1.0, but the macro is commented out:

- [`STTypes.h:185-189`](../../ThirdParty/soundtouch/include/STTypes.h#L185)

Without it, SoundTouch changes the order of its RateTransposer and TDStretch
stages and moves FIFO contents at the crossover:

- [`SoundTouch.cpp:218-260`](../../ThirdParty/soundtouch/source/SoundTouch/SoundTouch.cpp#L218)

Loop Breaker routes Speed through `setRate()` when pitch is inactive:

- [`AudioBuffer.cpp:1278-1282`](../../Source/AudioBuffer.cpp#L1278)
- [`AudioBuffer.cpp:1397-1400`](../../Source/AudioBuffer.cpp#L1397)

The parameter smoother does not prevent the internal topology change; it merely
determines when the crossing occurs.

**Fix experiment**

Add the definition to the SoundTouch implementation target. It does not alter
the public API or class layout, so keep it `PRIVATE`:

```cmake
target_compile_definitions(
    SoundTouch
    PRIVATE SOUNDTOUCH_PREVENT_CLICK_AT_RATE_CROSSOVER=1
)
```

Also update any Projucer/unity build that remains supported. A/B the documented
slight quality compromise and measure CPU cost. If unacceptable, detect the
crossover and perform it under a proper reset/re-prime transition.

**Required test**

- Stretch remains active.
- Speed alternates 0.5 to 2.0 and back.
- Pitch separately crosses zero, including -12 to +12.
- Inspect raw output, queue depth, underfills, and click energy as the smoother
  crosses 1.0.

### F-05 — The multi-block transition fade starts with old historical audio

The transition path is armed for repitch/stretch mode changes and SoundTouch
resets:

- [`AudioBuffer.cpp:243-303`](../../Source/AudioBuffer.cpp#L243)

`beginTransitionFade()` copies the oldest-to-newest portion of the last 20-80 ms
of output history:

- [`AudioBuffer.cpp:2844-2875`](../../Source/AudioBuffer.cpp#L2844)

At progress zero, `applyTransitionFade()` outputs 100% of
`transitionOldBuffer[0]`:

- [`AudioBuffer.cpp:2889-2901`](../../Source/AudioBuffer.cpp#L2889)

The previous callback actually ended at the newest old sample. The next callback
therefore jumps backward to the oldest sample in the captured tail, then replays
audio the listener has already heard. This is not a true crossfade between
simultaneous old and new future signals.

**Fix options**

- Zero-latency: retain the exact final emitted sample and apply a raised-cosine
  offset correction to the new stream.
- True crossfade: run old and new render branches concurrently and blend their
  future output.
- Fixed latency: delay emission so the old tail can be blended before it would
  otherwise be heard.

The existing ChannelStrip boundary correction demonstrates the correct
zero-latency shape:

- [`ChannelStrip.h:563-616`](../../Source/ChannelStrip.h#L563)

It is only armed for selected FX state operations, so it does not currently
protect AudioBuffer transport transitions.

### F-06 — Startup fade progress resets at each block

A 20-40 ms fade is scheduled here:

- [`AudioBuffer.cpp:2096-2106`](../../Source/AudioBuffer.cpp#L2096)

Each callback computes a local `fadeLen` and ramps from `1/fadeLen` to 1:

- [`AudioBuffer.cpp:2283-2295`](../../Source/AudioBuffer.cpp#L2283)

Only the remaining count persists. At 64 or 128 samples, the fade spans many
callbacks, but every callback falls back near zero and then reaches unity. That
creates a large downward step at every callback boundary.

**Fix**

Persist total length and global progress. Gain must be monotonic:

```text
gain = (globalProgress + localSample + 1) / totalFadeLength
```

or use a prepared `juce::SmoothedValue<float>`. Test with constant nonzero input
and assert the envelope never decreases before reaching unity.

### F-07 — Variable callback sizes discard rendered samples

Per-buffer scratch buffers are allocated to the advertised block size:

- [`PluginProcessor.cpp:224-236`](../../Source/PluginProcessor.cpp#L224)

During rendering, a scratch buffer is grown if too small but is never given the
current callback's logical sample count:

- [`PluginProcessor.cpp:824-834`](../../Source/PluginProcessor.cpp#L824)

`processSingleBuffer()` renders the entire scratch buffer:

- [`AudioBufferManager.cpp:96-117`](../../Source/AudioBufferManager.cpp#L96)

The merge copies only the host's current `numSamples`:

- [`PluginProcessor.cpp:837-854`](../../Source/PluginProcessor.cpp#L837)

If the host advertises 512 but delivers 128, Loop Breaker renders and advances
512 samples, emits 128, and discards 384. The next callback starts after a source
gap. This is a direct discontinuity mechanism and can also distort modifier and
SoundTouch queue timing.

**Fix**

Keep capacity preallocated, but render through a non-owning
`juce::AudioBuffer<float>` view whose logical length is exactly `numSamples`.
Do not resize/allocate on the audio thread. Prepare ChannelStrip DSP for the
advertised maximum in `prepareToPlay`; process only the active view length.

**Test**

Use callback sequences such as:

```text
512, 128, 512, 64, 256, 128, 512
```

Assert source advance and emitted sample count match every callback exactly.

### F-08 — Reverse mutates SoundTouch windows and allocates live

Reverse only negates speed:

- [`AppState.h:679-693`](../../Source/AppState.h#L679)
- [`AudioBuffer.cpp:1127-1134`](../../Source/AudioBuffer.cpp#L1127)

In SoundTouch mode, a direction change reconfigures the window settings without
starting a source or output transition:

- [`AudioBuffer.cpp:1402-1424`](../../Source/AudioBuffer.cpp#L1402)
- [`TimeStretchSoundTouch.h:44-57`](../../Source/TimeStretchSoundTouch.h#L44)

SoundTouch recalculates sequence and overlap state:

- [`TDStretch.cpp:101-140`](../../ThirdParty/soundtouch/source/SoundTouch/TDStretch.cpp#L101)

Increasing overlap deletes, reallocates, and clears the mid buffer:

- [`TDStretch.cpp:719-737`](../../ThirdParty/soundtouch/source/SoundTouch/TDStretch.cpp#L719)

**Fix**

- Configure a fixed, already-allocated window set during prepare, or keep
  separately prepared forward/reverse engines.
- Add a pivot-centered direction transition.
- Do not allocate/free in a render worker.
- If reset is unavoidable, perform it under the corrected output transition.

### F-09 — Transport and slice publication is not coherent

`setLoopWindow()` publishes `enabled=true` before the new start/end values:

- [`AudioBuffer.cpp:933-945`](../../Source/AudioBuffer.cpp#L933)

The renderers read enabled/start/end as separate atomics. A reader can see a
mixed generation.

Slice state has a load-then-later-store pattern. A trigger arriving after the
renderer's load but before its final store can be erased by the old local value:

- Repitch snapshot/writeback:
  [`AudioBuffer.cpp:567-577`](../../Source/AudioBuffer.cpp#L567) and
  [`AudioBuffer.cpp:920-923`](../../Source/AudioBuffer.cpp#L920)
- SoundTouch writeback:
  [`AudioBuffer.cpp:2084-2087`](../../Source/AudioBuffer.cpp#L2084)
- Producer:
  [`AudioBuffer.cpp:2369-2377`](../../Source/AudioBuffer.cpp#L2369)

Fold loop-window, seek, and slice changes into the generation-tagged command
design from F-02. Only the render owner should commit runtime active-slice and
playhead state.

The broader `AudioBufferParams` object is also shared mutable, non-atomic state:

- [`AudioBuffer.h:45-79`](../../Source/AudioBuffer.h#L45)
- [`AudioBuffer.h:445-456`](../../Source/AudioBuffer.h#L445)

It includes `isPlaying`, `isLooping`, slice counts/modes, counters, and a
`std::vector<int>` Arp sequence. UI/control methods can write those fields while
a render worker reads or mutates them. `atomicSpeed` addresses only speed. Move
the remaining control state into the same owned command/snapshot design; a
vector mutation racing a worker is undefined behavior, not merely a stale read.

### F-10 — Underfill recovery does not reconnect the next block

An underfilled block fades its tail toward zero and clears any remainder:

- [`AudioBuffer.cpp:2322-2356`](../../Source/AudioBuffer.cpp#L2322)

The gain formula uses `(fadeOut + 1)`, so a short deficient tail ends at
`last / (tail + 1)`, not exactly zero; a longer deficit has its remainder
cleared. No state forces the next successfully filled block to begin
continuously from that endpoint. It can start at arbitrary amplitude. Fix F-01
so underfills do not occur after warm-up, then retain an
`underfillRecoveryPending` state and apply a persistent next-block
correction/fade if one does occur.

### F-11 — Lookahead uses the wrong sample domain

The code correctly notes that source distance should be divided by the absolute
read step to obtain fill iterations, but then compares source-domain distance
directly to host-rate latency:

- [`AudioBuffer.cpp:1820-1852`](../../Source/AudioBuffer.cpp#L1820)

At `|step| > 1`, the boundary arrives before the input crossfade reaches its
assumed 50% handoff. Compute:

```text
iterationsToBoundary = ceil(sourceDistance / abs(step))
```

Use that value for lookahead start/length, and carry the actual terminal mix
factor into the post-boundary continuation.

### F-12 — DSP lifecycle and callback-size changes are not real-time safe

The per-buffer callback calls `prepareDSP()` from the render path:

- [`AppState.h:58-65`](../../Source/AppState.h#L58)

`prepareDSP()` resets processors and allocates/clears pre-delay, delay, chorus,
filter, and table storage whenever block size changes or on the first render:

- [`ChannelStrip.h:39-111`](../../Source/ChannelStrip.h#L39)

Move preparation to the host prepare lifecycle with maximum block size and
channel count. Combine this with the active buffer view in F-07. Add an
allocation hook in tests and require zero allocations/frees after warm-up.

The first SoundTouch `prepare()` also occurs inside
`processWithTimeStretch()` on a render worker:

- [`AudioBuffer.cpp:1377-1394`](../../Source/AudioBuffer.cpp#L1377)

Pre-prepare and warm the supported engine configuration outside render, or use a
prepared inactive engine that can be swapped in. Preallocating JUCE scratch does
not prevent SoundTouch itself from allocating.

SoundTouch lifecycle has a related problem: `AudioBuffer::prepare()` does not
unconditionally clear and mark the stretcher unprepared, while manager release
does not reset individual engines:

- [`AudioBuffer.cpp:142-203`](../../Source/AudioBuffer.cpp#L142)
- [`AudioBufferManager.cpp:133-141`](../../Source/AudioBufferManager.cpp#L133)

At an identical sample rate/channel count, stale pre-release pipeline state can
survive. Explicitly reset all pipeline, lookahead, ring, fade, and resampler
state on prepare/release.

### F-13 — Useful safeguards and tests were removed

Commit sequence:

- `d4cce11`: added a final AudioBuffer boundary de-clicker and a prepared 30 ms
  old/new delay-tap read-head crossfade.
- `8061643`: added `ClickDetectionTests.cpp`, but the test had no category while
  the standalone runner selected `"Click Detection"`, so the initial runner
  executed zero tests.
- `a1f9361`: assigned the category, fixed result-based failure counting, linked
  SoundTouch into the standalone runner, and made the test actually useful.
- `9e92bdb`: added raw pre-correction discontinuity inspection, next-block
  SoundTouch-underfill recovery, and host-lifecycle `prepareDSP()` integration.
- `a9b34b5`: removed the click tests and later additions.
- `3ae62f7`: removed the remaining final AudioBuffer de-clicker.

Current [`docs/plans/plan.md:99-124`](../plans/plan.md#L99) says the block
boundary de-clicker was completed, but current AudioBuffer has no
`applyDeClick()`. [`DECLICK_REPORT.md:264-345`](DECLICK_REPORT.md#L264) still
shows its implementation checklist as incomplete. Correct these documents when
the final design is chosen.

The removed test was not sufficient to restore unchanged:

- neither standalone click-test executable was registered with CTest
  `add_test()`;
- its assertions relied on debug-only tearing counters;
- Release `jassert` checks vanished;
- its first version observed post-de-click counters, while `9e92bdb` later made
  the counters inspect raw pre-correction output; that useful distinction still
  disappeared with the test;
- it did not exercise Switch Part, preset recall, raw seek, variable blocks,
  queue depth, underfill, or FX delay changes.

Restore a Release-independent analyzer that inspects samples directly. A final
boundary offset corrector is worthwhile as a safety net after F-01 through F-07,
but raw/pre-correction metrics must remain visible so it cannot hide structural
regressions.

### F-14 — Secondary transition bugs

These should be separate follow-up work, with tests:

- **Repitch reverse zero tail:** signed speed smoothing breaks rendering if the
  instantaneous speed is exactly zero, leaving the cleared remainder silent:
  [`AudioBuffer.cpp:565-599`](../../Source/AudioBuffer.cpp#L565). Treat zero as
  stationary playback or crossfade direction branches; do not break the block.
- **Exit slicing cancels a blend:** `exitSlicingMode()` immediately clears active
  input/lookahead crossfades:
  [`AudioBuffer.cpp:2486-2506`](../../Source/AudioBuffer.cpp#L2486). Stop future
  scheduling but allow the active transition to complete.
- **Ping-pong stale direction:** SoundTouch captures one immutable `step` for a
  fill, but ping-pong flips shared speed without updating that local step:
  [`AudioBuffer.cpp:1942-1963`](../../Source/AudioBuffer.cpp#L1942). Split the
  fill at the turn or update a mutable local step under an explicit pivot
  transition.
- **Crossfades longer than slices:** SoundTouch raises source crossfades to at
  least 50 ms:
  [`AudioBuffer.cpp:1258-1266`](../../Source/AudioBuffer.cpp#L1258). A short
  slice can start a new transition before the previous one completes. Cap
  duration relative to slice length or support overlapping transition voices.
- **SoundTouch parameters move in block-rate stairs:** each 50 ms smoother is
  sampled once, advanced over the rest of the callback with `skip(N - 1)`, and
  only that first value is sent to SoundTouch:
  [`AudioBuffer.cpp:1303-1400`](../../Source/AudioBuffer.cpp#L1303).
  At 48 kHz a 512-frame callback skips roughly 21.3% of the entire ramp between
  SoundTouch setter calls; 64 frames still skips about 2.7%. In addition,
  `pitchActive`/`useRate` routing is selected from the unsmoothed target, so a
  pitch zero-crossing can switch rate/tempo routing immediately. Use a bounded
  sub-block control cadence or an explicit transition protocol. Test
  active-to-active Stretch, Pitch, and Speed changes separately from mode entry
  and exit.
- **Delay tap jumps:** current delay read positions follow new times/layouts
  immediately:
  [`ChannelStrip.h:230-306`](../../Source/ChannelStrip.h#L230). Restore a
  prepared old/new read-head crossfade for Delay modifiers. The current local
  `juce::Array<int>` also builds the tap list inside every enabled callback;
  replace it with fixed-capacity prepared state.
- **Debug logging:** several `DBG` paths format strings on render workers,
  including mode transitions, direction changes, lookahead starts, and
  underfills. Desktop tearing diagnostics also default on and scan each output
  sample. Treat this as the wider real-time problem in F-17, not merely a Debug
  performance nuisance.

### F-15 — `-ffast-math` is a real but unproven build variable

SoundTouch's correlation and overlap search use floating-point reductions.
`-ffast-math` can change reduction order and occasionally select a different
splice candidate. It can also change denormal/NaN assumptions. No evidence here
proves that it causes the reported clicks, but the pre-existing vendored flags
became active in Loop Breaker's main build with the feed-loop change. The
separate target also forces these optimization semantics in Debug, unlike the
old unity build.

Keep the separate target and add an option that can compile it without
`-ffast-math`. Run the exact deterministic render and compare:

- maximum transition delta;
- high-frequency event energy;
- FIFO depth/underfills;
- output hashes away from transitions;
- callback duration.

Do not combine this A/B with other DSP fixes.

### F-16 — Render workers are not real-time-qualified

The processor creates three background workers by default:

- [`PluginProcessor.h:198`](../../Source/PluginProcessor.h#L198)

The pool starts ordinary `std::thread`s and only assigns an Apple thread name:

- [`RealtimeThreadPool.h:41-45`](../../Source/RealtimeThreadPool.h#L41)
- [`RealtimeThreadPool.h:131-163`](../../Source/RealtimeThreadPool.h#L131)

After a short idle spin, each worker repeatedly yields and sleeps for 100 µs.
The host audio thread participates in the jobs, but then spin-waits without a
timeout until every claimed task completes:

- [`RealtimeThreadPool.h:103-111`](../../Source/RealtimeThreadPool.h#L103)

At 48 kHz, 100 µs alone is 7.5% of a 64-frame callback budget, before OS wake
latency and rendering. More importantly, a normal-priority worker can be
descheduled after claiming a pad. The host thread cannot steal that in-progress
task and waits even if its own real-time deadline is imminent. Thread naming
does not grant real-time priority, QoS, core affinity, or Apple audio-workgroup
membership.

Denormal handling is also thread-local. `ScopedNoDenormals` covers only the host
callback thread:

- [`PluginProcessor.cpp:292-295`](../../Source/PluginProcessor.cpp#L292)

`AudioBuffer::prepare()` disables denormals on whichever thread calls prepare,
not on these persistent workers:

- [`AudioBuffer.cpp:142-145`](../../Source/AudioBuffer.cpp#L142)

This is a high-confidence real-time design defect, but static inspection and the
offline render do not prove that it caused the reported device pops.

**Required experiment and fix**

1. Add a supported runtime/build switch for zero workers versus the current
   three. On the same device/session, capture callback and per-pad task start/end
   timestamps, worker identity, deadline budget, late-callback count, and host
   xruns. Compare p50/p95/p99/p99.9/max rather than only average CPU.
2. Initialize no-denormal state once inside every worker.
3. Put workers into the platform's supported real-time audio scheduling/workgroup
   mechanism, or replace the pool with a proven audio-workgroup-aware executor.
4. Do not solve the wait by simply adding a timeout: abandoned tasks still
   mutate pad state and would race the next callback. A bounded failover design
   needs explicit per-task ownership/generations and a safe way for the host
   thread to claim work that has not started.
5. If the workers cannot be qualified, render serially on the host audio thread.
   Eight pads may cost more average CPU, but a predictable deadline can be safer
   than waiting on unqualified threads.

**Acceptance**

- No worker starts or completes after its owning callback generation.
- No callback exceeds its device deadline in a sustained worst-case eight-pad
  session, with a documented safety margin.
- Zero-worker and qualified-worker modes produce identical audio samples and
  transport state for a deterministic schedule.

### F-17 — The full callback still performs non-real-time work

Real-time correctness must cover the host callback, scheduler, effects, and
workers—not only `RealtimeThreadPool::processAll()`.

Concrete callback-path work includes:

- Constructing roughly 30 `juce::String` parameter IDs and looking them up in
  APVTS every block:
  [`PluginProcessor.cpp:446-465`](../../Source/PluginProcessor.cpp#L446).
- Updating a `std::map` of modifier weights:
  [`ModifierProbabilityManager.h:29-49`](../../Source/ModifierProbabilityManager.h#L29).
- Running the scheduler after pad rendering:
  [`PluginProcessor.cpp:861-873`](../../Source/PluginProcessor.cpp#L861).
  A due trigger copies descriptors/`juce::Array`s, takes `SpinLock`s, advances a
  `std::deque`, chooses replacement modifiers, and republishes queue state:
  [`ModifierScheduler.cpp:272-456`](../../Source/ModifierScheduler.cpp#L272).
  Weighted selection builds a new `std::vector`:
  [`ModifierProbabilityManager.h:62-102`](../../Source/ModifierProbabilityManager.h#L62).
- Constructing a local `juce::Array<int>` for delay taps inside every
  delay-enabled callback:
  [`ChannelStrip.h:229-245`](../../Source/ChannelStrip.h#L229).
- In desktop Debug builds, enabling the tearing analyzer by default, scanning
  samples, computing block statistics, performing atomic increments, and
  formatting `DBG` messages on render workers:
  [`AudioBuffer.h:344-352`](../../Source/AudioBuffer.h#L344) and
  [`AudioBuffer.cpp:326-496`](../../Source/AudioBuffer.cpp#L326).
  Additional unconditional Debug messages exist for MIDI and direction changes.

Most blocks may avoid the scheduler's expensive branch, which makes this a good
match for an *occasional* deadline artifact near modifier events. These code
paths do not prove a missed deadline; correlate them with device timing.

**Required fix**

1. Cache all APVTS raw parameter pointers during initialization and copy only
   atomic scalar values in the callback.
2. Replace enum-keyed maps and transient eligible vectors with fixed-capacity
   arrays.
3. Pre-plan modifier descriptors and targets outside the callback. Publish a
   fixed-capacity immutable snapshot or generation-tagged command; the callback
   should only consume it, never allocate or wait for a UI-held lock.
4. Prepare fixed-capacity delay tap state and restore the old/new read-head
   crossfade documented in F-13.
5. Default all string/log diagnostics off on every platform. Record bounded
   numeric events in a lock-free ring and format them on a non-audio thread.
   Keep continuity analysis in the offline test rather than scanning every
   device sample by default.
6. Audit modifier listeners and every `ChannelStrip` processor under an
   allocation/blocking-lock hook, including callbacks executed on the host
   thread.

**Acceptance**

- Zero allocation/free, blocking lock, string construction/formatting, file I/O,
  or unbounded loop anywhere in the full callback and its workers after prepare.
- No timing spike correlates with a modifier trigger, preset, part change, delay
  layout change, MIDI event, or debug-counter event.
- Device captures distinguish late callbacks/xruns from sample-domain click
  events so both classes can regress independently.

## Implementation plan for future sessions

### Work packet 1 — Regression harness and telemetry

Do this before changing algorithms.

1. Add a Release-independent `AudioContinuityTests.cpp` to the CMake test target.
2. Generate deterministic smooth sine, chirp, endpoint-mismatched loop, constant
   nonzero, and transient/drum-like fixtures.
3. Concatenate callbacks and analyze:
   - adjacent-sample delta;
   - high-passed short-window click energy;
   - zero runs;
   - NaN/Inf;
   - raw and corrected output separately.
4. Expose non-logging telemetry:
   - ready output before feed;
   - unprocessed input and current expected output-per-input ratio;
   - cumulative output-equivalent pipeline credit;
   - input fed/output received;
   - source write-head and command generation;
   - underfills/resets/recovery iterations;
   - callback deadline budget, wall time, worker/task start and finish, worker
     identity, late callback/xrun count, and post-warm-up allocation count.
5. Make current known failures explicit expected failures or recorded baselines so
   subsequent packets can demonstrate improvement.
6. Commit the harness source, exact event schedule/seed, compiler/link options,
   and raw machine-readable results. The probes used for this report were
   temporary research executables; the methods and representative results are
   preserved here, but the first future session should turn them into maintained
   CMake targets rather than attempting to reconstruct binaries.

### Work packet 2 — Stop catastrophic/boundedness failures

1. Disable the bulk-copy path for any file-boundary crossing (F-03).
2. Render exactly the active host callback length; never render and discard the
   unused scratch capacity (F-07).
3. Replace unconditional feed-first with a low/high-water controller and
   a signed output-credit ledger (F-01).
4. Add hard bounds/telemetry for FIFO depth and command-to-audible lead.
5. Verify small iOS blocks do not reintroduce starvation.

Keep priming chunking unless a test proves it is wrong.

### Work packet 3 — One transport-transition API

1. Add the fixed-capacity generation-tagged command path (F-02/F-09).
2. Migrate Switch Part, preset recall, Reset, host restart, global start offset,
   and slice triggers. Use the same coherent publication primitive for
   apply-part-after-load, but treat that fresh-buffer case as initialization
   rather than forcing an unnecessary audible crossfade.
3. Implement coherent loop-window + seek publication.
4. Implement mode-specific transition behavior and invalidate dependent state.
5. Delete or privatize the raw public `setPlayheadSamples()` path so new callers
   cannot bypass the protocol.

### Work packet 4 — SoundTouch transitions

1. A/B and likely enable rate-crossover prevention (F-04).
2. Fix global startup-fade progress (F-06).
3. Replace historical-tail replay (F-05).
4. Remove live reverse allocation and implement a real direction pivot (F-08).
5. Carry underfill recovery into the following callback (F-10).

### Work packet 5 — Host/RT correctness and safety net

1. A/B zero versus three workers on the target device, then qualify or remove
   the background worker pool (F-16).
2. Cache APVTS pointers, move modifier planning/publication off the callback,
   replace dynamic delay/selection containers, and remove render-thread
   formatting/locking (F-17).
3. Prepare ChannelStrip/SoundTouch outside render and reset lifecycle state
   coherently (F-12).
4. Fix lookahead units and secondary transition issues.
5. Restore a conservative block-boundary offset corrector, with raw diagnostics
   and an A/B toggle.
6. Correct stale de-click documentation.

## Validation matrix

### Audio configurations

- Sample rates: 44.1, 48, 88.2, and 96 kHz.
- Fixed blocks: 32, 64, 128, 256, 512, and 1024.
- Variable-block sequences, including advertised-max to short callbacks.
- Mono and stereo sources.
- One active pad and all platform-supported pads.
- Desktop Release, Debug with diagnostics off, and Debug with counters on.
- Actual iOS Standalone Release capture. Test AUv3 separately if the desktop
  AUv3 build remains supported. Record device model, OS, host, audio route,
  sample rate, requested/observed callback sequence, and thermal state.

### Event scenarios

1. Switch Part in repitch mode.
2. Switch Part with Stretch 0.5 and 2.0.
3. Switch Part with Pitch ±7 and ±12.
4. Switch Part with Stretch + Speed 0.5/2.0.
5. Preset recall while SoundTouch is active.
6. Reset and host restart while SoundTouch is active.
7. Global start offset.
8. Direct slice, random slice, Arp Slice, and Slice Repeater boundaries.
9. Enter and exit slicing at every crossfade phase.
10. Speed and pitch crossings through effective rate 1.0.
11. Forward/reverse transitions and ping-pong turns.
12. Whole-file and part-window loops with mismatched endpoints.
13. Mode transition between repitch and SoundTouch.
14. Forced underfill followed by recovery.
15. Delay time and multi-tap layout changes.
16. Stacked worst case: part switch + stretch + pitch + speed + reverse + slice.

### Acceptance criteria

- Smooth fixtures have no event delta above the established clean budget; use
  both absolute delta and local-RMS-normalized click energy.
- No NaN/Inf, unexplained zero run, or render watchdog timeout.
- No underfills after warm-up across the supported parameter matrix.
- FIFO and source-to-audible lead are bounded with zero long-run slope.
- Every command generation is applied exactly once within documented latency.
- Playhead and loop window invariants always hold.
- Exactly `N` output frames are processed and emitted for an `N`-frame
  callback; no rendered output is discarded. Source-input advance equals the
  queue controller's fully accounted feed count/ratio, including prime and
  recovery credit.
- No allocation, deallocation, lock, or string/log formatting occurs on render
  workers or elsewhere in the host callback after prepare.
- No device callback misses its deadline in the sustained worst-case matrix;
  report p50/p95/p99/p99.9/max timing and xruns.
- Corrected output passes perceptual thresholds, while raw counters remain
  available to expose masked defects.

## A/B and bisect procedure

Start with the same deterministic harness and identical source/event schedule:

1. `a534b9e` — immediate parent.
2. `ce269bd` — warning cleanup.
3. `0280da5`.
4. `213fdfc`.
5. `43fd95c` — audited HEAD.

After identifying `ce269bd`, split its variables:

1. Current code with only the old drain-first hunk restored.
2. Current controller with priming chunking retained.
3. Current external SoundTouch target with `-ffast-math` off.
4. Rate-crossover macro off/on.
5. Bulk fast path off/on with a watchdog.

Record the full output, not only pass/fail:

- delta/click event sample and associated command generation;
- FIFO ready/unprocessed depth at every block;
- source write-head versus audible transition;
- underfills and recovery;
- callback duration and allocation events.

Avoid a whole-commit revert as the final fix. Most warning changes are safe, the
chunked prime is valuable, and the parent drain-first path had its own
starvation/artifact behavior.

## Current test status

The existing Release test executable passed on the audited tree:

```text
./build/LoopBreakerTests_artefacts/Release/LoopBreakerTests
```

Current CMake includes no AudioBuffer continuity test:

- [`CMakeLists.txt:228-278`](../../CMakeLists.txt#L228)

The only SoundTouch test is a tiny compile/link/output smoke test:

- [`ModifierSchedulerTests.cpp:1005-1029`](../../Source/ModifierSchedulerTests.cpp#L1005)

Passing current tests therefore does not reduce the confidence of any audio
finding in this report.

## Recommended first implementation session

The safest first coding session should:

1. Add telemetry and a minimal Release continuity harness.
2. Disable boundary-crossing bulk copy.
3. Fix active callback-length rendering so no scratch tail is discarded.
4. Replace per-block `ceil()` with a signed output-credit ledger plus a bounded
   reserve.
5. Run the full fixed/variable block matrix.

The next session should implement the unified transport command and migrate all
raw seek call sites. Do not combine the queue controller, transport API,
rate-crossover macro, and transition-fade redesign in one commit; each needs an
independent A/B result.
