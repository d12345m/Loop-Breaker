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

## Pivot: VST Plugin Target (NEW)

The project is pivoting from “iOS-first app” to “VST3 plugin-first”. Standalone and iOS targets are being retired; new work should prioritize plugin requirements:

- [ ] Re-export/regenerate the macOS Xcode project from Projucer so old Standalone targets disappear (open BufferTest.jucer in Projucer and re-save/re-export)
- [ ] Decide whether to delete standalone-only source files (e.g. Source/Main.cpp, Source/MainAppComponent.\*) or keep them in-repo but excluded from the build
- [ ] Enable VST3 target in Projucer (.jucer)
- [ ] Define plugin bus layout for multi-output (8 buffers -> 8 distinct DAW outputs)
- [ ] Decide default mode: master-only stereo vs multi-out enabled by default (document)
- [ ] Add routing policy for hosts that don’t enable all outputs (document + fallback)
- [ ] Update UI/UX assumptions for plugin (no iOS-only pickers; drag-and-drop required)

---

## 1. UI Architecture Migration

- [x] Create `MainAppComponent` to replace `MainComponent` gradually
- [x] Integrate `AppState` inside new component (single ownership) (core settings + listeners wired; further settings UI exposure still pending)
- [x] Display: Pad grid (8 pads) + upcoming modifier display (top banner)
- [x] Add basic transport controls (Play All / Stop All)
- [x] Add per‑pad loaded file indicator (filename label beneath each pad)
- [x] Modal / panel for loading audio into each pad (desktop implementation via FileChooser; iOS adaptation pending)
- [x] Wire pad selections to scheduler (`setUserSelectedBuffers`) via direct pad selection callbacks (replaced polling)
- [x] Visual feedback (pad flash overlay) when modifier triggers (basic)
- [x] Expanded visual feedback / log area for modifier history (basic panel w/ last 100 events)
- [x] Per‑pad playing-state indicator (highlight/outline while buffer active)
- [x] Replace old `MainComponent` (now gated & excluded by default build)

### Plugin Editor UX (NEW)

- [x] Add drag-and-drop sample loading onto pads (desktop OS file drop)
- [ ] Keep click-to-load fallback (FileChooser) for hosts where drag/drop is limited
- [x] Visual affordance for drop target (per-pad highlight on drag enter)
- [ ] Failure UX: unsupported format / missing file / permission error (non-modal, logged)
- [ ] Modifier display tied to plugin window state: suppress modifier application when editor window is closed - this actually leads to weird issues where the modifiers are not applied or not displayed in certain situations.
- [ ] Upcoming modifier label hidden / inactive when plugin window is not shown - this is also a bug.
- [ ] Ensure modifiers resume immediately when editor window is re-opened

#### Modifier History Panel Enhancements (incremental)

- [x] Clear button to manually purge history
- [x] Display running count of total events (bounded by max entries)
- [x] Colour-code entries per modifier type (with legend)

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
- [x] Quantized trigger option (UI toggle + subdivision selector + resnap logic + unit test)
- [x] Drift resilience: use absolute nextTriggerTime (monotonic) instead of resetting window accumulator
- [x] UI: countdown / progress display (UpcomingModifierDisplay progress bar)
- [x] Suppression mode: maintain visual progress while preventing modifier firing when modifiers toggle off

### Plugin Timing Considerations (NEW)

- [ ] Confirm timeline source policy for plugin: BPM internal vs DAW tempo sync (needs decision)
- [ ] If DAW tempo sync is desired: map host tempo + transport state to scheduler (document)

---

## 3. Modifier System (Phase 1: Non‑Audio Logic)

- [x] Implement concrete classes for: Reverse, Speed, ResetAll
- [x] Apply logic to targeted buffers when modifier triggers (Reverse/Speed/ResetAll active)
- [x] Confirm multiple simultaneous buffer targets handled safely (basic unit test; UI marshalling added)
- [x] Logging hook: record triggered modifier type + targets (history panel currently logs descriptor only)
- [x] Basic random speed selection (.25, .5, 1, 2) with inclusive list
- [x] Ensure ResetAll semantics: preserve currently playing buffers (speed->1, direction forward, slices exited) while keeping playback running
- [ ] Provide dry run mode (no side effects) for preview (optional)
- [x] Scheduler option: restrict random selection to implemented modifiers

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

## Multi-Output Routing (DAW) (NEW)

Goal: each of the 8 buffers can be routed to its own DAW output channel/bus.

- [x] Implement per-buffer output assignment (buffer i -> output bus j)
- [ ] Ensure master FX chain is compatible with both master-only and multi-out modes
- [x] Verify behavior in at least one multi-out-capable host (manual QA checklist)
- [ ] Add debug display showing which outputs are currently active/enabled by host

Constraints:

- [ ] Host may not activate all outputs; plugin must not crash and must produce sensible audio
- [ ] No dynamic allocations or file I/O on the audio thread

---

## 6. Beat Slicing & Advanced Buffer Modifiers

- [ ] Unit test: determinism when seeded
- [ ] New variation - repeater. repeat slice 2/4/8/16 times before moving to next slice.

---

## 7. Pitch & Time Features

- [ ] Investigate and fix octave pitch shift audio artifacts (crackle/distortion on Oct+/Oct−)
- [ ] Consider limiting pitch range (e.g., max ±2 octaves) to reduce artifact severity
- [ ] Validate pitch shift quality across different sample rates and buffer sizes

---

## 9. Parts (Musical Sections A–D)

- [x] Allow user to define part boundaries (equal length constraint as per design)
- [x] UI to switch active part (affects playback start offsets?)
- [x] Potential modifier: switch part randomly (future)
- [x] Persistence of part definitions

---

## 10. Multi‑Channel Recording

- [ ] Ensure master FX applied post individual FX but pre master record (design doc constraint)

---

## 12. Theming & UX Polish

- [ ] Theme system (struct + palette) with runtime switching
- [ ] Adaptive contrast & accessible font sizing
- [ ] Pad color states: idle, selected, recently modified, active playback
- [ ] Animations (GPU safe, avoid heavy repaints every frame)
- [ ] Custom control styles (knobs, sliders, buttons) beyond default JUCE LookAndFeel
- [ ] Animated transitions and feedback (smooth pad interactions, modifier trigger visuals)
- [ ] Loop progress visualization: circular/ring display instead of linear progress bar
- [ ] Header area layout improvements (element sizing, spacing, visual hierarchy)
- [ ] Improved modifier naming and value display (human-readable labels, formatted parameter values)
- [ ] Better variable/value presentation for modifier parameters in UI

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

## 17b. Host / File Access Considerations (Plugin) (NEW)

- [ ] Document expected file access model (absolute paths, missing-file recovery)
- [ ] Decide whether to copy imported samples into a project-managed folder vs reference-in-place
- [ ] Ensure graceful handling when file paths become invalid

---

## 18. Release Engineering

- [ ] App icon & branding
- [ ] Build configurations: Debug / Release / Profiling
- [ ] Conditional compilation flags for dev tools
- [ ] CI pipeline (future) – build & run tests on push
- [ ] Crash reporting integration (future consideration)

### 18a. Installer Creation

Research (2026-02-18): JUCE has no built-in installer tool but provides an [official tutorial](https://juce.com/tutorials/tutorial_app_plugin_packaging/) covering the process end-to-end.

- [ ] **macOS**: Create `.pkg` installer using [Packages](http://s.sudre.free.fr/Software/Packages/about.html) (free). Target: `/Library/Audio/Plug-Ins/VST3/` (system) or `~/Library/Audio/Plug-Ins/VST3/` (user). Wrap in `.dmg` disk image.
- [ ] **Windows**: Create `.exe` installer using [Inno Setup](http://www.jrsoftware.org/isinfo.php) (free). Target: `C:\Program Files\Common Files\VST3\`.
- [ ] Alternative macOS option: simple drag-and-drop `.dmg` with folder alias to VST3 directory
- [ ] Code-sign macOS build (Apple Developer ID + `codesign` + notarization via `xcrun notarytool`)
- [ ] Code-sign Windows build (`signtool` + EV certificate if needed)

### 18b. Licensing & Copy Protection

Research (2026-02-18): JUCE provides a **built-in licensing framework** via `OnlineUnlockStatus`, `OnlineUnlockForm`, `KeyGeneration`, and `RSAKey` classes. This is an RSA key-pair system where the plugin embeds a public key and your server holds the private key. Users authenticate via email+password, the server returns a signed key file cached locally for offline use. **You must build and host your own server backend** (PHP/Node/Python). There is no built-in storefront, trial management, or payment processing.

Third-party alternatives popular in the JUCE community:

| Service                                | Scope                                                                                                                                                                                                | Cost                                                             |
| -------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| **[Moonbase.sh](https://moonbase.sh)** | End-to-end: storefront, RSA-signed license tokens, online/offline activation, trials, payment processing (Merchant of Record, handles VAT). SDKs available. Recommended by multiple JUCE forum devs. | Free tier (10k validations/mo), then €5/10k; 5% + €1/transaction |
| **PACE / iLok**                        | Industry-standard dongle + cloud licensing. Very secure but expensive and complex (users must install iLok License Manager).                                                                         | Enterprise pricing (~$1k+ setup)                                 |
| **Gumroad**                            | Simple storefront + payments. No native plugin licensing — pair with JUCE's `OnlineUnlockStatus`.                                                                                                    | 10% flat per sale                                                |
| **Roll your own**                      | JUCE `OnlineUnlockStatus` + simple web backend + Stripe for payments                                                                                                                                 | Stripe fees (~2.9% + $0.30)                                      |

**Decision (2026-02-18): Use [Moonbase.sh](https://moonbase.sh) as the licensing, storefront, and payment provider.** Rationale: turnkey end-to-end solution (storefront, RSA-signed license tokens, online/offline activation, trials, Merchant of Record with VAT handling). Free tier is sufficient during development. Avoids building and hosting a custom server backend.

- [x] Decide licensing strategy → **Moonbase.sh**
- [x] Create Moonbase account & set up product for BufferTest
- [x] Integrate Moonbase SDK into the plugin (license validation on startup)
- [x] Implement in-plugin activation UI (email/key entry overlay in plugin editor)
- [x] Support offline license activation (Moonbase RSA-signed tokens)
- [ ] Configure trial mode in Moonbase (time-limited or feature-limited demo)
- [ ] Test full license flow: purchase → activate → offline use → machine transfer → revoke
- [ ] Handle license failure gracefully in plugin (locked/demo state, no crash, clear messaging)

### 18c. Purchase Flow & Distribution

- [ ] Set up Moonbase storefront (product page, pricing variations, discount/upgrade rules)
- [ ] Configure Moonbase payment processing (pricing in USD/EUR, subscription vs lifetime options)
- [ ] Host downloadable installers via Moonbase hosting (or own site with Moonbase handling licensing)
- [ ] Auto-update mechanism (future — evaluate Sparkle on macOS, WinSparkle on Windows, or manual check)

### 18d. JUCE License for This Project

Note (2026-02-18): JUCE 8 itself requires a license for closed-source commercial distribution:

| Tier    | Cost                                  | Revenue Cap   |
| ------- | ------------------------------------- | ------------- |
| Starter | Free                                  | $20,000/year  |
| Indie   | $40/user/month (or $800 perpetual)    | $300,000/year |
| Pro     | $175/user/month (or $3,500 perpetual) | Unlimited     |

- [ ] Start with Starter (free) during development
- [ ] Upgrade to Indie before commercial release if revenue will exceed $20k
- [ ] Document JUCE license tier in project README

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

Status: Items 1–4,6 completed; Item 5 pending.

---

## 21. Audio Artifact Mitigation (Experimental / Deferred)

These items were prototyped to address clicks on rapid speed changes & reversals. Decision: defer and potentially roll back code to simplify baseline until broader DSP roadmap (Sections 5 & 6) advances.

- [x] Reverse direction transition crossfade (equal-power) – EXPERIMENTAL (to be removed in rollback)
- [x] Large same-direction speed jump crossfade with threshold detection – EXPERIMENTAL (to be removed in rollback)
- [x] Parameterization of reverse & speed-jump crossfade lengths + threshold ratio – EXPERIMENTAL
- [ ] Zero-cross alignment for transition points (Deferred)
- [ ] Adaptive crossfade length scaling by speed delta magnitude (Deferred)
- [ ] Formal measurement: pre/post click energy comparison (Deferred)
- [ ] Configurable UI exposure of crossfade parameters (Deferred)

Rollback Plan:

1. Remove added crossfade state (reverse & speed-jump) from `AudioBuffer`.
2. Retain existing slice crossfade + speed smoothing only.
3. Keep checklist section so rationale & approach aren't lost.

---

## 22. MIDI Integration & Automation

Goal: allow external MIDI control and DAW automation of modifier probability sliders and key parameters.

- [x] Expose modifier probability sliders as automatable plugin parameters (`AudioProcessorValueTreeState` with one `AudioParameterFloat` per modifier type, IDs `prob_N`)
- [x] Add MIDI CC mapping to probability sliders (enable MIDI faders, knobs, LFOs to control probabilities) — CC→param map stored in `SessionSettings::midiProbCCMap`, handled in `processBlock`
- [x] Define MIDI learn workflow for probability controls — click CC button on Modifiers tab, move a hardware/software CC; click again to cancel; right-click to clear
- [x] Ensure MIDI-driven parameter changes are thread-safe and applied on the audio thread — APVTS atomics used throughout; APVTS values synced → `probManager` each block
- [ ] Document supported MIDI CC ranges and default mappings (CC 0-127, all unassigned by default)
- [ ] Test with common DAW automation lanes and external MIDI controllers

---

## 23. Documentation & Attribution

- [ ] Document keyboard shortcuts (README or in-plugin help overlay)
- [ ] Add attribution for third-party libraries (timestretching algorithm, SoundTouch, etc.)
- [ ] In-plugin About/Credits panel with version info and third-party attributions
- [ ] User guide / quick-start documentation (PDF, web page, or in-repo markdown)
- [ ] Developer documentation: build instructions, architecture overview, contribution guide

---

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
- 2025-10-01: Integrated UpcomingModifierDisplay progress bar & suppression mode; scheduler now absolute-time with quantization backend; Reverse/Speed/ResetAll modifiers fully active with ResetAll preserving playback.
- 2025-10-01: Implemented experimental reverse & speed-jump crossfades (artifact mitigation); decision made to defer & plan rollback; checklist updated to reflect true current state.

- 2026-02-09: Pivot decision: prioritize VST3 plugin target; added checklist sections for VST enablement, multi-output routing, and drag-and-drop sample loading.
- 2026-02-18: Added items: plugin window modifier behavior (suppress when editor closed), pitch shift artifact investigation & range limits, MIDI CC / automation for probability sliders, documentation & attribution section, installer / licensing / distribution items, UI enhancement goals (custom styles, loop circle, header layout, modifier naming).

- 2026-02-19: Implemented MIDI CC control for modifier probability sliders (Section 22). Added `AudioProcessorValueTreeState` to `PluginProcessor` exposing all 22 modifier probability parameters for DAW automation. Added MIDI CC learn per slider in the Modifiers tab (click CC button → move hardware/software CC → auto-mapped; right-click to clear). CC assignments persisted in plugin state JSON. `processBlock` syncs CC→APVTS→probManager on audio thread.
