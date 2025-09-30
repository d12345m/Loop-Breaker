# Implementation Checklist

This evolving checklist tracks the work needed to move from the current proof-of-concept to a production‑ready iOS app matching the design goals.

Legend:

- [ ] Not started
- [-] In progress / partially complete
- [x] Complete (merged & verified)
- (?) Needs clarification before work can start

---

## 0. Foundations & Current State

- [x] Core `AudioBuffer` class with speed, reverse, slicing support
- [x] `AudioBufferManager` mixing multiple buffers
- [x] Basic desktop test UI (`MainComponent`) for single buffer
- [x] Architecture skeleton: modifiers, scheduler, project manager, session settings
- [x] Pad grid & upcoming modifier display components (not yet integrated into runtime UI)
- [x] Unified main app UI using new architecture (legacy component gated off by macro)

---

## 1. UI Architecture Migration

- [x] Create `MainAppComponent` to replace `MainComponent` gradually
- [-] Integrate `AppState` inside new component (single ownership) (basic integration done; more exposure of settings forthcoming)
- [x] Display: Pad grid (8 pads) + upcoming modifier display (top banner)
- [x] Add basic transport controls (Play All / Stop All)
- [x] Add per‑pad loaded file indicator (filename label beneath each pad)
- [x] Modal / panel for loading audio into each pad (desktop implementation via FileChooser; iOS adaptation pending)
- [x] Wire pad selections to scheduler (`setUserSelectedBuffers`) via direct pad selection callbacks (replaced polling)
- [x] Visual feedback (pad flash overlay) when modifier triggers (basic)
- [x] Expanded visual feedback / log area for modifier history (basic panel w/ last 100 events)
- [x] Per‑pad playing-state indicator (highlight/outline while buffer active)
- [x] Replace old `MainComponent` (now gated & excluded by default build)

#### Modifier History Panel Enhancements (incremental)

- [ ] Clear button to manually purge history
- [ ] Filter modes: All / Buffer-targeted only / Global-only
- [ ] Display running count of total events (bounded by max entries)
- [ ] Colour-code entries per modifier type (with legend)
- [ ] Configurable max entries (auto-trim setting in SessionSettings)
- [ ] (Persistence) integrate with existing project save once core persistence extended (see Section 8)

### Nice to Have (Defer until basics work)

- [ ] Animated pad press feedback
- [ ] Theme switching (light/dark placeholder)

---

## 2. Timing & Scheduler Accuracy

- [x] Feed `ModifierScheduler::updateTime` from audio callback (sample accurate) instead of timer
- [x] Maintain musical timeline (bars / beats) using BPM + sampleRate (basic accumulators)
- [x] Expose next trigger ETA in seconds + bars (status label integration)
- [ ] Unit test: Trigger exactly every N bars (tolerance < 1 audio block)
- [ ] Support changing BPM at runtime (scheduler recalculates remaining time proportionally)
- [ ] Support changing barsBetweenModifiers mid-cycle (policy: adjust next trigger using remaining proportion)
- [ ] Quantized trigger option (align to nearest beat/subdivision even if window elapsed mid-beat)
- [x] Drift resilience: use absolute nextTriggerTime (monotonic) instead of resetting window accumulator
- [ ] UI: optional countdown display (progress bar or beat pulses)

---

## 3. Modifier System (Phase 1: Non‑Audio Logic)

- [ ] Implement concrete classes for: Reverse, Speed, ResetAll (logic only; uses existing buffer controls)
- [ ] Apply logic to targeted buffers when modifier triggers
- [ ] Confirm multiple simultaneous buffer targets handled safely
- [ ] Logging hook: record triggered modifier type + targets
- [ ] Basic random speed selection (.25, .5, 1, 2) with inclusive list
- [ ] Ensure ResetAll restores: speed=1, direction forward, slices exited, effects off (future‑proof)
- [ ] Provide dry run mode (no side effects) for preview (optional)

---

## 4. Modifier System (Phase 2: Buffer FX Placeholders)

- [ ] Define effect parameter envelope structure (start value, target value, duration bars, curve type)
- [ ] Implement effect state container per channel (delay, reverb, filters, tremolo)
- [ ] Hook modifiers to schedule envelopes (data only)
- [ ] Process envelopes each block (update parameters) – no audio DSP yet
- [ ] UI debug panel to inspect live envelope states

---

## 5. DSP Effect Implementation (Phase 3)

- [ ] Add JUCE `dsp::ProcessorChain` per `ChannelStrip` with: Gain, IIR LPF, IIR HPF, DelayLine, Tremolo (custom LFO), Reverb
- [ ] Thread-safe parameter updates from envelopes
- [ ] Implement volume ramp down via gain smoothing
- [ ] Implement tremolo (LFO depth & rate envelope capable)
- [ ] Master chain: HPF + LPF available (enable/disable via master modifiers)
- [ ] Performance audit: no dynamic allocations per audio block

---

## 6. Beat Slicing & Advanced Buffer Modifiers

- [ ] Extend slicing to musically quantized divisions (1/4, 1/8, 1/8T, 1/16, 1/32, 1/64)
- [ ] Random reorder playback sequence generator (precomputed index map)
- [ ] Ensure no new buffer copies (logical addressing only)
- [ ] Crossfade strategy validation for tiny slice sizes (avoid clicks)
- [ ] Configurable slice division selection (random vs chosen)
- [ ] Unit test: determinism when seeded

---

## 7. Pitch & Time Features

- [ ] Integrate a phase vocoder / elastique-like timestretch (investigate licensing; else open-source alternative)
- [ ] If stretch not available: fallback rate-change only (already present)
- [ ] Pitch up/down octave independent of speed if timestretch available
- [ ] Cache/resample strategy performance test per buffer

---

## 8. Persistence & Projects

- [ ] Extend project JSON to include: buffer file paths, last slice count, per-part definitions
- [ ] Save / load selected theme, BPM, time signature, barsBetweenModifiers
- [ ] Save modifier history (for debugging) optionally
- [ ] Implement New / Save / Load / Rename UI
- [ ] iOS Documents directory integration
- [ ] Handle missing files gracefully (placeholder state)

---

## 9. Parts (Musical Sections A–D)

- [ ] Allow user to define part boundaries (equal length constraint as per design)
- [ ] UI to switch active part (affects playback start offsets?)
- [ ] Potential modifier: switch part randomly (future)
- [ ] Persistence of part definitions

---

## 10. Multi‑Channel Recording

- [ ] Architectural decision: multi-out vs offline stem render
- [ ] If multi-out: add additional buses (desktop & iOS support differences)
- [ ] If offline bounce: implement export pipeline mixing each buffer soloed
- [ ] UI toggle for multi-channel mode
- [ ] Ensure master FX applied post individual FX but pre master record (design doc constraint)

---

## 11. iOS Platform Integration

- [ ] Replace desktop file chooser with UIDocumentPicker bridge
- [ ] Optimize UI layout for portrait/landscape
- [ ] Touch interactions: larger pads, velocity? (future)
- [ ] Background audio session category configuration
- [ ] Power / CPU profiling on device
- [ ] iOS specific entitlements (file access, audio)

---

## 12. Theming & UX Polish

- [ ] Theme system (struct + palette) with runtime switching
- [ ] Adaptive contrast & accessible font sizing
- [ ] Pad color states: idle, selected, recently modified, active playback
- [ ] Animations (GPU safe, avoid heavy repaints every frame)

---

## 13. Logging & Debug Tooling

- [ ] Central log buffer (ring) with levels (Info/Warning/Error/Modifier)
- [ ] On-screen collapsible debug console (dev builds only)
- [ ] Export log with project save
- [ ] Performance stats overlay (RT vs message thread timing)

---

## 14. Testing Strategy

- [ ] Introduce JUCE `UnitTest` module usage or Catch2 (decision)
- [ ] Unit tests: scheduler timing, modifier selection randomness distribution
- [ ] Unit tests: reset correctness of buffers & effects
- [ ] Property-based test: random sequences of modifiers keep audio engine stable (no crashes)
- [ ] Benchmark harness: measure average `processBlock` time with N active modifiers

---

## 15. Performance & Stability

- [ ] Lock-free queues / message passing for modifier activation -> audio thread
- [ ] Avoid heap allocs in audio thread (audit new code paths)
- [ ] SIMD optimize interpolation if profiling warrants
- [ ] CPU usage budget per buffer & total (document targets)
- [ ] Memory footprint tracking (buffers + FX states)

---

## 16. Error Handling & Resilience

- [ ] Graceful handling of failed file loads
- [ ] Validation of BPM/time signature inputs (clamp ranges)
- [ ] Detect & prevent invalid slice divisions (e.g., 0 length)
- [ ] Watchdog for scheduler drift (re-sync on large deviation)

---

## 17. Security / Sandbox Considerations (iOS)

- [ ] Confirm file access limited to user-chosen locations
- [ ] Remove diagnostic exports in release builds
- [ ] Harden JSON parsing (validate schema)

---

## 18. Release Engineering

- [ ] App icon & branding
- [ ] Build configurations: Debug / Release / Profiling
- [ ] Conditional compilation flags for dev tools
- [ ] CI pipeline (future) – build & run tests on push
- [ ] Crash reporting integration (future consideration)

---

## 19. Stretch / Future Ideas (Do NOT implement yet)

- [ ] Modifier chaining / combinators (meta-modifiers)
- [ ] User-created modifiers (scripting)
- [ ] Network sync / collaborative session
- [ ] MIDI pad / controller integration
- [ ] Export stems + master simultaneously

---

## 20. Immediate Next Sprint (Recommended Order)

1. New `MainAppComponent` with pad grid + upcoming modifier UI
2. Scheduler fed by audio callback time (sample accurate)
3. Implement Reverse + Speed + ResetAll modifiers (real logic)
4. Wire pad selections to targeted buffer logic & visual feedback
5. Introduce first unit tests (scheduler + reset)
6. Replace legacy `MainComponent`

---

## Open Questions (Need clarification)

- (?) Should speed modifier choose from fixed set only or also include random envelope over duration?
- (?) Do slice divisions apply simultaneously per buffer or selectable per-buffer?
- (?) For master FX ordering: confirm final chain layout (per-channel -> sum -> master FX -> recording) vs design variant.
- (?) Should modifier trigger time quantize if user changes BPM mid-cycle (immediate recalculation or hold)?

Add clarifications inline as decisions are made.

---

## Progress Log (append entries)

- 2025-09-30: Initial architecture skeleton & checklist created.
- 2025-09-30: Added per-pad filename labels, direct selection callbacks, pad flash animation; updated checklist statuses.
- 2025-09-30: Implemented per-pad playing-state indicator (green outline updated via timer), unified UI refinement.
