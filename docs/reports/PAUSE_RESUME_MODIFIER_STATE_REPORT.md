# DAW Pause/Resume Modifier-State Loss — Investigation and Implementation Plan

**Date:** 2026-07-22  
**Status:** Root cause confirmed; implementation not started  
**Confidence:** High  
**Scope:** Plug-in resource lifecycle, asynchronous sample loading, modifier-state preservation, and SoundTouch resume behavior

---

## 1. Executive Summary

Loop Breaker sometimes loses part of a pad's modifier stack after a user pauses and resumes playback in a DAW. The root cause is an unnecessary sample reload during certain host resource-lifecycle sequences.

Some DAWs call `releaseResources()` while pausing, suspending, or reconfiguring a plug-in. Loop Breaker currently interprets any such call as a reason to reload every configured pad from disk during the next `prepareToPlay()` or `processBlock()` call. This is incorrect because decoded sample data is intentionally retained across resource release.

When the redundant background load completes, `AudioBuffer::setLoadedAudioData()` treats the result as genuinely new audio. It resets source-dependent and transient DSP state, including:

- playhead position;
- loop-window state before the active part is reapplied;
- slicing activation and current slice state;
- continuous random slicing;
- time-stretch ratio and SoundTouch state;
- crossfade, lookahead, and transition state.

Other state is not reset, including speed, reverse direction, pitch, ping-pong, and most ChannelStrip effects. This produces the user's observed selective or apparently random modifier loss.

The primary correction is to prevent resource release from scheduling a state-restoration reload. The recovery path must also become pad-specific, because the current self-healing logic reloads every pad when only one pad is missing.

---

## 2. User-Visible Symptoms

The exact symptom depends on which modifiers are stacked and when the asynchronous reload completes.

### 2.1 Selective modifier loss

After pause/resume, a pad may retain:

- Speed;
- Reverse;
- Pitch Up/Down;
- Ping Pong;
- Delay, Reverb, filters, and other ChannelStrip effects.

The same pad may lose or effectively disable:

- Stretch;
- Beat Slice;
- Arp Slice;
- Slice Repeater;
- current slice and transient playhead position.

This selectivity occurs because those states live in different objects and `setLoadedAudioData()` resets only some of them.

### 2.2 Delayed or silent restart

If the redundant load completes after DAW playback has resumed, `AudioBufferManager::applyPendingLoads()` classifies the pad as newly loaded while transport is active. It then:

1. replaces the audio data;
2. stops the pad if it was playing;
3. marks it as `awaitingMusicalStart`.

The pad remains silent until a bar boundary or modifier trigger calls `startBuffersAwaitingMusicalCue()`.

### 2.3 Intermittent reproduction

The behavior depends on:

- whether the DAW calls `releaseResources()` for pause/suspend;
- whether the reload finishes while transport is stopped or playing;
- sample decode time;
- host format and wrapper behavior;
- the active modifier combination.

This makes the bug appear timing-dependent even though the state destruction itself is deterministic once a replacement load is applied.

---

## 3. Confirmed Failure Chain

### Step 1: Host releases resources

`BufferTestAudioProcessor::releaseResources()` checks whether any pad path exists. If so, it sets `pendingRestoreReload` after releasing manager resources.

Current location:

- `Source/PluginProcessor.cpp`, `releaseResources()` around lines 242–264.

The flag name implies session restoration, but it is also being used for ordinary host lifecycle events.

### Step 2: Reload is scheduled on prepare or process

Both `prepareToPlay()` and `processBlock()` consume `pendingRestoreReload` and call `reloadBuffersFromPadPaths()`.

Current locations:

- `Source/PluginProcessor.cpp`, `prepareToPlay()` around lines 235–237;
- `Source/PluginProcessor.cpp`, `processBlock()` around lines 625–627.

### Step 3: Every configured pad is decoded again

`reloadBuffersFromPadPaths()` iterates over all eight pads and schedules a background load for every nonempty path.

Current location:

- `Source/PluginProcessor.cpp`, `reloadBuffersFromPadPaths()` around lines 1160–1184.

It does not check whether a pad already has valid decoded audio.

### Step 4: Completed load replaces existing audio

`AudioBufferManager::applyPendingLoads()` passes every successful decoded result to `AudioBuffer::setLoadedAudioData()`.

Current location:

- `Source/AudioBufferManager.cpp`, `applyPendingLoads()` around lines 306–353.

### Step 5: Replacement resets transient modifier state

`AudioBuffer::setLoadedAudioData()` resets playback and transient DSP state for a clean start. That behavior is reasonable for a genuinely new sample, but destructive for an identical lifecycle reload.

Current location:

- `Source/AudioBuffer.cpp`, `setLoadedAudioData()` around lines 1009–1050.

Notable resets include:

```cpp
playheadPosition.store(0.0);
clearLoopWindow();
slicingModeActive.store(false);
sliceTriggered.store(false);
currentActiveSlice.store(0);
targetSlice.store(0);
params.continuousRandomSlicing = false;
stretchRatio.store(1.0);
stretcher.reset();
```

Arp-specific parameter fields are not fully cleared here, but `slicingModeActive` is disabled, leaving inconsistent stale metadata with inactive slice processing.

### Step 6: Active-transport completion defers the pad

When transport is active, `applyPendingLoads()` marks the pad as awaiting a musical start. This behavior is appropriate for an intentional mid-transport sample load but not for an identical lifecycle reload.

---

## 4. Broader Self-Healing Defect

The current self-healing path checks whether any configured pad lacks audio and whether there are no global pending loads. If one missing pad is found, it calls the same all-pad `reloadBuffersFromPadPaths()` function.

Current location:

- `Source/PluginProcessor.cpp`, self-healing block around lines 653–670.

Example:

1. Pad 1 is loaded and has Stretch plus Beat Slice.
2. Pad 2 has a configured path, but its earlier load was interrupted.
3. Self-healing notices Pad 2 has no audio.
4. The code reloads Pad 1 and Pad 2.
5. Pad 1's valid audio is replaced and its transient modifier state is reset.

Therefore, merely removing the `releaseResources()` reload flag would fix the common pause/resume path but would leave a second way for valid pads to lose modifiers.

---

## 5. Historical Findings

### `11916e5` — retain decoded audio across re-preparation

This commit explicitly changed `AudioBuffer::releaseResources()` so that it would not clear decoded audio during bus reconfiguration or host re-preparation.

This established the intended lifecycle contract: decoded samples survive resource release.

### `370c774` — introduced the conflicting reload policy

This commit added:

- the pad-path scan in `releaseResources()`;
- unconditional re-arming of `pendingRestoreReload`;
- the broad self-healing reload.

This is the origin of the persistent modifier-loss path.

### `3ae62f7` — not the root cause

`Restore application to click/pop remediation plan state` removed parts of the de-click and delay-transition work. It did not introduce the path-based lifecycle reload.

### `48f274b` — retained more resources but left reload behavior

This commit changed resource management for late AU render callbacks and retained manager/buffer processing resources across release. It did not remove the older reload policy.

### JUCE 9 release-build change

`e601425` changed the macOS release workflow from JUCE 8.0.6 to JUCE 9.0.0. No relevant DSP or resource-lifecycle source has changed since `48f274b`.

JUCE 9 may have changed how reliably a particular host sequence exposes the old bug. This is a plausible trigger for the bug appearing more frequently, but it is not proven by repository history and is not the underlying state-loss mechanism.

---

## 6. Required Design Corrections

### 6.1 Keep resource release non-destructive

`BufferTestAudioProcessor::releaseResources()` should no longer inspect pad paths or set a reload flag.

Target behavior:

```cpp
void BufferTestAudioProcessor::releaseResources()
{
    resourcesPrepared.store(false, std::memory_order_release);
    app.bufferManager.releaseResources();
}
```

Responsibilities of resource release should be limited to:

- gating render callbacks;
- stopping or joining background jobs safely;
- releasing resources that genuinely require re-preparation;
- retaining decoded audio and modifier parameters.

It must not reinterpret existing file paths as a request to replace valid audio.

### 6.2 Separate restoration from recovery

The current `reloadBuffersFromPadPaths()` function combines two different operations:

1. full session-state reconciliation;
2. recovery of a missing or interrupted pad load.

These should become separate functions with separate contracts.

Suggested processor helpers:

```cpp
void restoreBuffersFromSessionState();
void recoverMissingBuffersFromPadPaths();
```

### Full session restoration

`restoreBuffersFromSessionState()` should:

- run only because valid `setStateInformation()` data was accepted;
- reconcile all pad paths from the serialized state;
- schedule loads for nonempty restored paths;
- explicitly clear pads represented as empty in restored state;
- invalidate older requests using per-pad generations;
- log missing or unreadable files without affecting unrelated pads.

### Missing-pad recovery

`recoverMissingBuffersFromPadPaths()` should:

- inspect each pad independently;
- skip pads that already have audio;
- skip pads that already have an equivalent request in flight;
- schedule only missing pads with nonempty paths;
- never clear or replace a valid pad.

### 6.3 Rename the pending state flag

Rename:

```cpp
pendingRestoreReload
```

to something explicitly restricted to session state, for example:

```cpp
pendingSessionStateRestore
```

Only `setStateInformation()` should set it. `releaseResources()` must not touch it.

### 6.4 Introduce explicit load intent

Asynchronous loads currently carry only index, decoded data, source path, and success. Add an intent so result application can distinguish user actions from recovery and restoration.

Suggested enum:

```cpp
enum class AudioLoadIntent
{
    UserReplacement,
    SessionRestore,
    MissingDataRecovery
};
```

Suggested pending result fields:

```cpp
struct PendingLoadedBuffer
{
    int bufferIndex = -1;
    AudioBuffer::LoadedAudioData::Ptr data;
    juce::String sourcePath;
    uint64_t generation = 0;
    AudioLoadIntent intent = AudioLoadIntent::UserReplacement;
    bool ok = false;
};
```

Policy:

| Intent | Valid existing audio may be replaced? | Deferred start while playing? | Failure clears current audio? |
| --- | --- | --- | --- |
| User replacement | Yes | Yes | No |
| Session restore | Yes, if result is current | Yes when appropriate | Only if restored state explicitly says empty |
| Missing-data recovery | No; pad must still be empty | Yes | No |

### 6.5 Track request identity per pad

Each pad should have a monotonically increasing load generation or request token.

Recommended behavior:

1. Increment the pad's generation whenever a new request supersedes older work.
2. Store that generation in the load job and pending result.
3. When applying a result, compare it with the current generation.
4. Discard stale results without modifying the pad.
5. Optionally compare the result path with the pad's latest requested path.

Suggested manager state:

```cpp
std::array<std::atomic<uint64_t>, MAX_BUFFERS> latestLoadGeneration {};
std::array<std::atomic<int>, MAX_BUFFERS> outstandingLoadCounts {};
```

The exact pending-state implementation must account for:

- jobs canceled before execution;
- jobs already decoding during `removeAllJobs()`;
- results waiting in `pendingFifo`;
- rapid user replacement requests;
- manager destruction.

The counter or pending marker must be cleared on all completion, cancellation, rejection, and stale-result paths.

### 6.6 Make failed loads non-destructive

`applyPendingLoads()` currently calls `clearBuffer()` after a decode failure. This can erase valid in-memory audio if a replacement or redundant load fails.

New failure policy:

- Log the failure with pad number, path, intent, and generation.
- Preserve current audio if the pad already has valid data.
- Leave a missing pad empty if recovery fails.
- Clear a pad only when the user or restored state explicitly requests an empty pad.
- Do not let a stale failed result affect a newer request.

### 6.7 Preserve intentional new-sample reset semantics

Do not globally remove resets from `AudioBuffer::setLoadedAudioData()`.

Those resets are correct when the source audio genuinely changes because playhead, loop, slice, and crossfade state depend on sample length and content. The primary correction is to ensure identical retained audio is not replaced during lifecycle recovery.

If replacement policies are later added to `setLoadedAudioData()`, they should be explicit rather than inferred from transport state.

---

## 7. Proposed Control Flow

### 7.1 Ordinary DAW pause/release/resume

```text
releaseResources()
  -> gate rendering
  -> safely stop loader jobs
  -> keep decoded audio and modifier state

prepareToPlay()
  -> reallocate/prepare DSP scratch resources
  -> do not schedule sample reloads

processBlock()
  -> resume existing pads with unchanged modifier parameters
```

### 7.2 Session state restoration

```text
setStateInformation()
  -> validate and store restored pad paths/settings
  -> invalidate older per-pad requests
  -> set pendingSessionStateRestore

prepareToPlay() or safe scheduling point
  -> restoreBuffersFromSessionState()
  -> schedule only the requests required by restored state

applyPendingLoads()
  -> ignore stale generations
  -> apply current results using SessionRestore policy
```

### 7.3 Interrupted-load recovery

```text
processBlock() recovery check
  -> if no equivalent request is pending:
       for each pad:
         if path exists and pad has no audio:
           schedule MissingDataRecovery for that pad only

applyPendingLoads()
  -> apply only if generation is current and pad is still empty
  -> never replace a valid pad
```

---

## 8. SoundTouch Pause/Resume Follow-Up

This is a separate issue from persistent modifier loss.

`AudioBuffer::setPlaying(false)` always sets `stretcherNeedsReset`. On the next stretched block, `processWithTimeStretch()` resets and re-primes SoundTouch, then applies a startup fade.

Consequences can include:

- a brief fade or gap after resume;
- loss of the prior SoundTouch overlap tail;
- reported source playhead moving ahead during priming;
- a small audible continuity change.

The stretch ratio itself remains stored, so this path does not explain modifiers being persistently cleared.

Recommended sequence:

1. Fix redundant sample replacement first.
2. Re-test pause/resume with Stretch active.
3. If a transient remains, distinguish pause/suspend from stop/seek.

Potential API split:

```cpp
void pausePreservingDSPState();
void stopAndResetDSPState();
```

Policy candidate:

- Pause followed by resume at the same host timeline position: preserve the SoundTouch pipeline.
- Seek, restart, source replacement, or discontinuous timeline jump: reset and re-prime SoundTouch.

This follow-up requires host-timeline discontinuity tests and should not be bundled blindly into the primary reload correction.

---

## 9. Implementation Checklist

Legend:

- [ ] Not started
- [-] In progress
- [x] Complete and verified

### Phase 1 — Stop lifecycle-triggered replacement

- [ ] Remove the pad-path scan from `BufferTestAudioProcessor::releaseResources()`.
- [ ] Remove `pendingRestoreReload.store(true)` from `releaseResources()`.
- [ ] Rename `pendingRestoreReload` to `pendingSessionStateRestore`.
- [ ] Ensure only successful `setStateInformation()` parsing sets the renamed flag.
- [ ] Update lifecycle comments so resource release and state restoration are not conflated.
- [ ] Confirm `prepareToPlay()` preserves speed, stretch ratio, pitch, slicing mode, ping-pong, and FX parameters.
- [ ] Confirm no redundant background sample job is queued after release/prepare.

### Phase 2 — Make recovery pad-specific

- [ ] Split full session restoration from missing-pad recovery.
- [ ] Add `recoverMissingBuffersFromPadPaths()`.
- [ ] Skip every pad for which `hasAudioLoaded()` is true.
- [ ] Skip pads with empty paths.
- [ ] Prevent duplicate recovery requests for the same pad/path.
- [ ] Remove the call from the self-healing block to the all-pad restoration function.
- [ ] Verify one missing pad cannot replace any loaded pad.

### Phase 3 — Harden asynchronous requests

- [ ] Add `AudioLoadIntent`.
- [ ] Add per-pad request generations.
- [ ] Attach generation and intent to `LoadJob` and `PendingLoadedBuffer`.
- [ ] Discard stale successful results.
- [ ] Discard stale failed results.
- [ ] Ensure rapid A-then-B user loads leave B installed.
- [ ] Track pending state per pad or provide `hasPendingLoadForPad()`.
- [ ] Clear pending state on success, failure, cancellation, stale discard, and manager destruction.
- [ ] Audit `removeAllJobs(true, 5000)` interaction with pending markers.

### Phase 4 — Make failure safe

- [ ] Stop clearing valid current audio after a failed decode.
- [ ] Preserve current modifier state after a failed replacement.
- [ ] Keep missing recovery pads empty after failure.
- [ ] Log intent and generation with failures.
- [ ] Ensure missing session files affect only their own pad.
- [ ] Add non-modal UI feedback for failed user loads if desired.

### Phase 5 — Regression tests

- [ ] Add a helper that generates a deterministic temporary WAV for tests.
- [ ] Add a way to wait for and apply asynchronous load results deterministically.
- [ ] Add a test-only load counter or data identity accessor if needed.
- [ ] Add processor lifecycle tests described below.
- [ ] Add stale-request and failure-policy tests.
- [ ] Run existing JUCE unit tests.
- [ ] Run pluginval at the configured strictness.
- [ ] Perform manual AU and VST3 host validation.

### Phase 6 — SoundTouch follow-up

- [ ] Measure resume gap after the reload fix.
- [ ] Record SoundTouch reset and underfill counters across pause/resume.
- [ ] Decide same-position pause semantics versus stop/seek semantics.
- [ ] Implement a DSP-preserving pause path only if testing shows it is needed.
- [ ] Add output-continuity assertions with Stretch, Pitch, and Speed combinations.

---

## 10. Detailed Regression-Test Matrix

### 10.1 Lifecycle state preservation

Set up a pad with deterministic audio, then apply a modifier stack and perform:

```text
prepareToPlay
render blocks
releaseResources
prepareToPlay
render blocks
```

Assertions:

- audio remains loaded;
- no replacement load is queued;
- playhead follows the selected transport policy;
- pad is not marked `awaitingMusicalStart` because of resource release;
- modifiers retain their pre-release values.

Run separate cases for:

| Case | Stack | Required preserved state |
| --- | --- | --- |
| A | Stretch + Speed | stretch ratio and signed speed |
| B | Stretch + Pitch | stretch ratio and semitone shift |
| C | Beat Slice + Speed | slicing active, slice count, speed |
| D | Arp Slice + Stretch | arp active, sequence configuration, stretch |
| E | Slice Repeater + Pitch | repeater mode, repetition configuration, pitch |
| F | Ping Pong + FX | ping-pong settings and ChannelStrip effect state |

### 10.2 Pad-specific recovery

Setup:

- Pad 1: loaded and modified.
- Pad 2: valid path but no decoded audio.
- Pad 3: loaded and modified.

Trigger self-healing.

Assertions:

- only Pad 2 receives a load request;
- Pad 1 and Pad 3 keep the same audio identity;
- Pad 1 and Pad 3 retain modifier state;
- only Pad 2 may receive deferred-start treatment.

### 10.3 Active-transport intentional replacement

While transport is playing, intentionally load a different file into one pad.

Assertions:

- only the target pad is replaced;
- reset-to-new-source semantics occur on that pad;
- the target pad waits for a musical cue if that remains the desired policy;
- unrelated pads continue playing and retain modifiers.

### 10.4 Stale request ordering

Queue file A and then file B for the same pad before A completes.

Assertions:

- A's result is discarded if it completes after being superseded;
- B is the final installed source;
- a failed A result cannot clear B;
- pending state returns to idle.

### 10.5 Failure behavior

Test:

- unreadable or unsupported replacement file;
- deleted file during recovery;
- missing file during session restoration;
- queue-full rejection;
- canceled job during resource release.

Assertions:

- existing valid audio is preserved;
- unrelated pads are unchanged;
- pending markers do not remain stuck;
- logs identify pad, path, intent, and generation.

### 10.6 Host and wrapper matrix

At minimum, test the DAW in which the issue was observed using:

| Variable | Values |
| --- | --- |
| Format | AU, VST3 |
| JUCE | 8.0.6, 9.0.0 |
| Buffer size | 64, 128, 256, 512, 1024 |
| Sample rate | 44.1 kHz, 48 kHz, 96 kHz |
| Transport action | pause/resume, stop/play, seek while stopped, loop-cycle boundary |
| Modifier stack | no stretch, stretch, stretch + slicing, stretch + pitch |

Log or observe:

- `prepareToPlay()` and `releaseResources()` call counts;
- sample load request/apply counts;
- modifier parameters before and after lifecycle calls;
- `awaitingMusicalStart` transitions;
- SoundTouch resets and underfills.

The JUCE 8/9 comparison determines whether the wrapper upgrade changes reproduction frequency, while the code-level assertions ensure behavior is correct regardless of callback order.

---

## 11. Acceptance Criteria

The primary bug is considered fixed when all of the following are true:

- Pausing and resuming the DAW never queues a replacement load for a pad that already has valid audio.
- Resource release and re-preparation preserve the complete active modifier stack.
- Recovery of one missing pad does not replace or reset another pad.
- A stale asynchronous result cannot overwrite a newer sample request.
- A failed load cannot clear valid existing audio unless an explicit empty-state operation requested it.
- Intentional mid-transport new-sample loads retain their intended deferred-start behavior.
- Session restoration continues to load configured pad files and clear pads explicitly stored as empty.
- AU and VST3 pass manual pause/resume validation in the target DAW.
- Existing scheduler and SoundTouch unit tests continue to pass.

The optional SoundTouch follow-up is considered complete when same-position pause/resume produces no unacceptable gap or discontinuity without allowing stale pipeline audio after a seek or restart.

---

## 12. Recommended Implementation Order

1. Add regression tests that demonstrate the current release/prepare reload and all-pad recovery behavior.
2. Remove lifecycle re-arming of state restoration.
3. Split restoration and missing-pad recovery.
4. Make recovery pad-specific.
5. Add load intent, generations, and stale-result rejection.
6. Make failure handling non-destructive.
7. Run unit tests and pluginval.
8. Validate AU and VST3 in the affected DAW.
9. Reassess SoundTouch pause behavior only after the persistent modifier loss is gone.

This order fixes the user-facing defect early while retaining test coverage for the more extensive asynchronous-load hardening.

---

## 13. Files Expected to Change During Implementation

Primary files:

- `Source/PluginProcessor.cpp`
- `Source/PluginProcessor.h`
- `Source/AudioBufferManager.cpp`
- `Source/AudioBufferManager.h`
- `Source/ModifierSchedulerTests.cpp` or a new dedicated lifecycle test file
- `CMakeLists.txt` if a new test source file is added

Possible follow-up files:

- `Source/AudioBuffer.cpp`
- `Source/AudioBuffer.h`
- `Source/PluginEditor.cpp` for improved user-load failure feedback

`AudioBuffer::setLoadedAudioData()` should not require semantic changes for the primary fix if unnecessary reloads are correctly prevented.

---

## 14. Out of Scope for the Primary Fix

- Persisting active runtime modifier stacks in DAW session state.
- Redesigning the scheduler or modifier selection system.
- Changing intentional new-sample reset behavior.
- General click/pop remediation unrelated to pause/resume.
- Broad SoundTouch algorithm changes before the reload fix is validated.
- Assuming a JUCE 9 regression without an A/B host reproduction.

These may be valid future projects, but none is required to correct the confirmed lifecycle-driven modifier loss.
