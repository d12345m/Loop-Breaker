# Loop Breaker — Control Surface Visual Design Brief

- Status: implementation in progress; visual foundation, first glyph pass, canonical registry, planned queue, first Session geometry pass, headless test runner, and downstream planning audit complete
- Branch: `codex/hieroglyph-ui-concepts`
- Last implementation review: 2026-07-22
- Primary visual reference: `docs/concepts/hieroglyph-ui/07-control-surface.png`
- Secondary motion reference: `docs/concepts/hieroglyph-ui/08-cartography.png`

## 1. Purpose

This document is the durable visual and technical brief for future implementation conversations. It should be read before changing the Session tab, modifier scheduler, theme system, or modifier-display components.

The approved direction is **Control Surface**: a light, flat, modular interface influenced by late-1970s utilitarian science-fiction control graphics and the playful clarity of Teenage Engineering products. The secondary **Cartography** concept contributes selective waviness and contour motion, but is not the base layout.

“Alien” refers to the production design and interface language of the 1979 film—not literal aliens, creatures, biomechanical imagery, or horror motifs.

## 2. Reference material

Project mockups:

- `docs/concepts/hieroglyph-ui/06-paper-terminal.png` — first successful light direction.
- `docs/concepts/hieroglyph-ui/07-control-surface.png` — approved structural and visual baseline.
- `docs/concepts/hieroglyph-ui/08-cartography.png` — reference for restrained contour animation.

External influences:

- **1979 Alien production design:** ivory illuminated control tiles, black technical markings, strict modular grids, restrained primary-color signals, and systems that feel specialized rather than generic.
- **Teenage Engineering / OP-1:** playful diagrams, confident use of color, controls that explain behavior visually, occasional deliberate weirdness, and an instrument-like feeling.
- **Early vector and CRT instrumentation:** fine rules, simple geometry, calibration marks, stepped graphs, scan/contour behavior, and limited-color displays.
- **Hieroglyphic communication:** the modifier's behavior should be recognizable from shape and motion before its label is read.

These are principles, not instructions to reproduce any protected screen, icon, logo, or product exactly. All Loop Breaker glyphs must be original.

## 3. Design thesis

Loop Breaker should feel like a friendly specialized instrument—not a settings dashboard and not a conventional audio-engineering panel.

The Session tab should prioritize, in order:

1. What will happen next?
2. Which pads will it affect?
3. How long until it happens?
4. What comes after it?
5. What audio is loaded and currently playing?

Modifier communication uses progressive disclosure:

- **Queue state:** compact glyph, small name, minimal or no parameter text.
- **Next state:** large animated glyph, modifier name, meaningful planned variant values, target indicators, and timer.
- **Menus/settings:** normal language remains appropriate. MIDI learn, CC assignment, accessibility, file operations, probabilities, and configuration should favor clarity over symbolism.

## 4. Core visual language

### 4.1 Geometry

- Use a strict modular grid and mostly square or lightly softened corners.
- Prefer thin charcoal rules (approximately 1–2 logical pixels) to shadows.
- Avoid pill-shaped containers except when the shape conveys a specific control.
- Use generous internal margins so fine glyphs do not feel cramped.
- Maintain the existing four-column, two-row pad geography: pads 5–8 above pads 1–4.
- Preserve the A–H targeting strip immediately above the pad grid.

### 4.2 Palette

The interface should be approximately 85% neutral and 15% signal color.

Suggested starting palette:

| Role | Suggested color | Purpose |
|---|---:|---|
| Canvas | `#ECE8DC` | Warm ivory base |
| Raised tile | `#F5F1E7` | Subtle module separation |
| Display | `#121316` | Waveform and instrument apertures |
| Ink | `#202124` | Primary rules and text |
| Muted ink | `#77756E` | Secondary values and unavailable states |
| Vermilion | `#F04B35` | Current/urgent/primary selection |
| Safety yellow | `#E6BF3A` | Alternate state or timing accent |
| Signal green | `#54A866` | Active/valid/positive signal |
| Ultramarine | `#3159C9` | Alternate target/channel accent |
| Violet | `#8246AF` | Additional target/channel accent |

Do not use broad gradients or neon glow as general decoration. Color behaves like printed ink or an indicator lamp.

### 4.3 Typography

- Use a narrow, technical display face for labels and values.
- Use uppercase for navigation, modifier names, and short status labels.
- Keep filenames readable and mixed-case where appropriate.
- Text must remain the fallback decoder for every glyph.
- Do not place explanatory prose on the Session tab.
- Retain prose in Help, Settings, contextual menus, accessibility descriptions, and tooltips.

### 4.4 Lines and marks

- Primary glyph stroke: roughly 2 logical pixels at the 1200×800 reference size.
- Secondary/calibration stroke: roughly 1 logical pixel.
- Use dots, short ticks, brackets, underlines, stepped lines, and sparse arrows.
- Slight asymmetry or imperfect spacing can add personality, but semantic geometry must remain clear.
- Wavy/contour lines are reserved for time dispersion, modulation, spectral motion, or granularity. They should not decorate unrelated controls.

## 5. Session-tab layout

### 5.1 Top navigation

Retain the existing tabs: Session, Probability, Settings, Debug (development builds), and Help. The active tab uses a short vermilion underline rather than a filled tab.

### 5.2 Logo and timer

The existing timer integration with the Loop Breaker logo is worth preserving.

The implemented combination is:

- Keep the countdown/progress behavior integrated with the wordmark.
- Do not add a second progress rule beneath the wordmark; the wordmark fill and
  the full-width rule beneath the modifier board already communicate timing.
- The detailed countdown still belongs with the large “next” modifier glyph.
- Timer motion must not distort or reduce the legibility of the logo.

### 5.3 Modifier control board

The center header becomes a modular ivory control board:

- One large **next** cell, approximately twice the width of each queue cell.
- Two or three smaller **queued** cells.
- The large cell contains the main glyph, `NEXT`, the modifier name, planned variant details, and a compact modifier-family marker.
- Queue cells contain a compact glyph, short name, and optionally one essential variant value.
- The master-volume control remains at the far right.
- No descriptive modifier sentence appears here.

### 5.4 Preset strip and target cues

- Preserve A–H as a single horizontal preset strip aligned to the eight pads.
  These slots retain their existing save/recall behavior; they are not modifier
  target selectors.
- Explicit modifier targets continue to come from pad selection. Selected pads
  use a short colored underline plus a small circular lamp, not a bright full
  perimeter.
- The top-right pips identify the modifier family using the same groups as the
  Probability tab: Buffer is one ultramarine pip, Channel Effect is two green
  pips, and Special is three vermilion pips. The redundant count-and-colour
  coding keeps the marker useful without relying on colour alone.
- Occupied preset slots use a visibly tinted fill plus a small status lamp, so
  saved state remains obvious at a glance. Pending recalls use a restrained
  underline/tint while keeping their text readable without color.

### 5.5 Pads

- Retain eight pads and current waveform/playhead behavior.
- Loaded pads: ivory outer tile with an inset black waveform aperture.
- Empty pads: ivory tile with a centered load glyph made from a plus and four corner brackets.
- Pad number remains top-left; MIDI note remains top-right; filename remains below the display aperture.
- Selection uses a colored underline, corner tab, or status lamp—not a neon full perimeter.
- Default waveform remains readable and relatively conventional.
- Contour lines may appear as a low-contrast modifier overlay or short triggered animation. Do not permanently obscure transient information.

## 6. Motion language

Animation should clarify signal behavior, not prove that the UI can animate.

Global rules:

- Prefer 15–30 FPS; the existing UI already uses 15 Hz for several animations.
- Keep most loops between 1.2 and 3 seconds.
- Use linear or gently eased motion; avoid elastic/bouncy easing.
- Animate only the large next glyph continuously.
- Queue glyphs should normally be static, with at most a very slow phase change.
- Triggered pad overlays should decay within roughly 300–800 ms.
- Modifier-glyph motion is enabled by default and has visible enable/speed
  controls in Settings. Disabling it is the reduced-motion path.
- Add a reduced-motion path: static representative frame, no loss of meaning.
- Do not allocate, load assets, or perform expensive geometry construction on the audio thread.

Cartography rules:

- Nested contours represent propagation, blur, modulation, density, or spectral movement.
- Use approximately 3–7 contours in normal UI elements.
- Keep opacity low behind waveforms.
- Cache reusable paths or precompute normalized geometry when practical.

## 7. Modifier glyph system

### 7.1 Rendering architecture

Implement glyphs as JUCE vector drawing code, not a library of raster images.

Recommended API shape:

```cpp
struct ModifierGlyphState
{
    ModifierDescriptor descriptor;
    float phase01 = 0.0f;
    float emphasis01 = 1.0f;
    bool compact = false;
    bool reducedMotion = false;
};

class ModifierGlyphRenderer
{
public:
    static void draw(juce::Graphics&,
                     juce::Rectangle<float> bounds,
                     const ModifierGlyphState&,
                     const ControlSurfacePalette&);
};
```

Use normalized coordinates and separate semantic construction from component layout. The same renderer must serve:

- the large next display;
- compact queue tiles;
- the Glyph Lab;
- deterministic snapshot/export tests;
- future documentation images.

Do not create one JUCE component subclass per modifier. Prefer dispatch by `ModifierType` into small pure drawing functions.

### 7.2 Glyph checklist

The current `ModifierType` enum contains the following representations. The table includes every enum entry other than `Unknown`, even where scheduling lists differ.

| Modifier | Static hieroglyph | Large/next animation | Compact/queue treatment | Planned values to show |
|---|---|---|---|---|
| Reverse | Three direction chevrons folding back across a center line | Chevrons travel right, hinge, then return left | Two reverse chevrons | None |
| Speed | Parallel timing ticks compressed on one side and expanded on the other | Tick spacing smoothly contracts/expands | Three unequal ticks + arrow | Speed multiplier |
| Stretch | Two boundary posts with a waveform pulled between them | Boundary posts separate while waveform amplitude remains stable | Posts + double-headed arrow | Stretch ratio |
| Pitch Up Octave | Stepped contour rising to a second register line | A signal dot climbs one octave step | Upward stair + dot | `+1 OCT` |
| Pitch Down Octave | Stepped contour descending to a lower register line | A signal dot descends one octave step | Downward stair + dot | `−1 OCT` |
| Beat Slice Random | A divided bar with selected segments jumping out of order | Segment markers reshuffle at beat boundaries | Four blocks in nonsequential order | Slice division |
| Arp Slice | A staircase of slice cells arranged by pitch/order | Highlight visits cells in a deterministic shuffled order | Four ascending blocks with shuffled highlight | Sequence length / repeat bars |
| Slice Repeater | Four slice cells with one selected repeat source | Selected slice pulses `on → off → on → off`, then advances to the next shuffled slice | One filled active cell + three outline cells | Repetitions / grid count |
| Ping Pong | One signal dot bouncing between L and R posts | Dot traverses left/right with a simple trail | L–R posts + opposing arrows | Musical division |
| Buffer Delay On | Original pulse followed by diminishing echo arcs | Echo copies propagate outward and fade | Three nested echo arcs | Division, wet, feedback; indicate ping-pong/wow only if planned |
| Buffer Delay Dub Burst | Tape/feedback coil feeding a send gate and three spaced echo taps | Input pulse charges the coil; gate opens and echo-tap spacing expands | Coil + gate + red/gold/green taps | Delay division / feedback where available |
| Buffer Reverb On | Central impulse surrounded by increasingly irregular contour rings | Rings expand, wobble, and dissolve | Three irregular nested contours | Wet and fade bars |
| Buffer Low Pass On | High stepped terrain clipped by a descending cutoff line | Cutoff line sweeps downward/left; high steps disappear | Descending diagonal over steps | Fade bars / immediate-jump mode |
| Buffer High Pass On | Low stepped terrain clipped by an ascending cutoff line | Cutoff line sweeps upward/right; low steps disappear | Ascending diagonal over steps | Fade bars / immediate-jump mode |
| Buffer Volume Ramp Down | Bespectacled librarian with a bun shushing a talker | The talker's voice marks contract and disappear while the raised finger remains fixed | Librarian profile, raised finger, and diminishing voice marks | Fade and hold bars |
| Buffer Tremolo | Stable center line wrapped by alternating amplitude lobes | Lobes pulse symmetrically around the center | Alternating high/low marks | Rate/depth if later planned |
| Buffer Chorus On | One central trace with two slightly offset wavy companions | Companion traces drift in and out of phase | Three offset waves | Rate, depth, mix |
| Buffer Auto Pan | Signal dot orbiting between L and R half-circles | Dot sweeps laterally; depth controls travel range | L/R arc with dot | Rate, depth, mix |
| Buffer Ducking On | Large foreground pulse pressing a second signal below a threshold | Secondary line dips whenever foreground pulse arrives | Pulse over downward notch | None currently; ducking remains always-on/non-prototyped |
| Buffer S&H Low Pass On | One irregular held-cutoff trace crossed by a descending filter slash | A single random-looking pattern regenerates only at discrete hold intervals | One stepped trace + low-pass slash | S&H division |
| Buffer S&H High Pass On | One irregular held-cutoff trace crossed by an ascending filter slash | A single random-looking pattern regenerates only at discrete hold intervals | One stepped trace + high-pass slash | S&H division |
| Buffer Granular On | Cloud of small grains distributed around a central waveform seed | Grains appear, drift, and recombine continuously | Sparse dot cloud | Density, size, pitch spread, mix, fade |
| Buffer Granular Momentary | Compact grain packet that erupts and collapses | Short outward grain burst, then snap back | Dense dot packet + burst ticks | Density, size, pitch spread, mix |
| Master High Pass On (retired legacy ID) | Legacy high-pass glyph spanning all eight target ticks | Retained only for old saved data and debug rendering | High-pass slash + `ALL`/eight dots | Not scheduled or shown in production controls |
| Master Low Pass On (retired legacy ID) | Legacy low-pass glyph spanning all eight target ticks | Retained only for old saved data and debug rendering | Low-pass slash + `ALL`/eight dots | Not scheduled or shown in production controls |
| Switch Part | Two numbered page tiles with a transfer notch | Front tile slides away and next tile indexes in | Overlapping pages + arrow | Destination part if known |
| Quarter Note Burst | Four evenly spaced impact marks on a bar ruler | Playhead strikes each quarter mark | Four dots on a line | Burst bars |
| Swap Modifier Stack | Two containers holding stacked layers with crossing transfer paths | Layers cross between containers while retaining color/order | Two stacks + crossing arrows | Target pips; no prose |
| Reset All | Divergent marks collapse to one neutral origin | Colored states retract into a central zero | Converging arrows + zero dot | None |

### 7.3 Variant visualization

Variant data should affect the glyph where it is semantically useful:

- Faster rates increase animation frequency, but clamp for legibility.
- Wet/mix values change the balance between source and processed marks.
- Fade bars change envelope slope or contour expansion rate.
- Slice divisions change visible segmentation.
- Delay feedback changes echo count/opacity.
- Granular density and size change dot count and radius.
- Auto-pan depth changes lateral travel width.

Never require the user to estimate an exact value from animation. The large next view must still display concise numeric or musical text.

## 8. Queue backend requirement

### 8.1 Current implementation

`ModifierScheduler` now owns a depth-three `std::deque<PlannedModifier>`. Each
entry contains a prepared descriptor and a target array frozen against the
current loaded-pad/selection state. When pad availability or explicit selection
changes, queued target snapshots are refreshed and rebroadcast before they can
fire. The existing
`getUpcomingModifier()` and `upcomingModifierChanged` API remain as compatibility
views of the front item, while `getPlannedQueueSnapshot()` and
`plannedQueueChanged` expose copied queue state to the UI.

Queue snapshot notifications are coalesced through `juce::AsyncUpdater` and
delivered on the message thread. Trigger callbacks remain synchronous because
they drive application/DSP state. Queue access itself is protected while copied.

### 8.2 Implemented model

The scheduler owns a planned queue with default depth **3**:

```cpp
static constexpr int kPlannedQueueDepth = 3;
std::deque<PlannedModifier> plannedQueue;

struct PlannedModifier
{
    ModifierDescriptor descriptor;
    juce::Array<int> targets;
    // Optional future timing metadata if cadence can vary per entry.
};
```

The descriptor must be fully variant-planned when it enters the queue. Descriptors
remain frozen until they fire. Target arrays remain frozen between relevant input
changes; loading/clearing pads or changing the explicit pad selection replans and
rebroadcasts the queued targets before the front entry can fire. This preserves
truthful previews without allowing a queue created before sample loading to target
empty pads.

Explicit pad selection is live performance input for every pad-targeted modifier,
not a commitment made when the modifier first enters the queue. Users may select or
deselect pads until the final moment before trigger; each change must synchronously
refresh the queued target snapshots and target pips so the front entry consumes the
latest eligible selection. Modifier-specific planning must never silently restore,
supplement, or retain pads the user has deselected.

Public surface:

```cpp
std::vector<PlannedModifier> getPlannedQueueSnapshot() const;
```

Listener callback:

```cpp
virtual void plannedQueueChanged(const std::vector<PlannedModifier>&) {}
```

The UI receives snapshot copies and does not hold references into scheduler-owned
storage. Planning and queue replenishment can still originate from audio/host
timeline processing; allocation and locking cost therefore remain explicit items
for the performance audit before release.

### 8.3 Queue behavior checklist

- [x] Fill queue to depth 3 on scheduler start when at least one modifier has non-zero probability.
- [x] Front item is the next modifier and owns the active countdown.
- [x] On trigger, pop front and append one newly planned item.
- [x] `skipUpcoming()` pops front without firing, then replenishes.
- [x] `forceUpcomingModifier()` replaces only the front item; `forcePlannedModifier()` can replace an explicitly selected queue slot for debug/testing.
- [x] Debug tooling can set each queue position independently through the Debug modifier panel's `NEXT` / `QUEUE 1` / `QUEUE 2` selector.
- [x] Probability/settings changes affect newly enqueued items, not already-previewed entries.
- [x] Suppression advances the queue consistently with current trigger semantics.
- [x] Reset/start/stop behavior is deterministic in the implemented queue lifecycle.
- [x] Seeded randomness produces reproducible full queues.
- [x] Queue never contains `Unknown`; all-zero probability produces an empty queue and a musical cue.
- [x] Structured planned variants shown in the UI come from the same frozen descriptor consumed at trigger time. Switch Part freezes and displays an explicit destination; Volume Ramp freezes and displays both fade and hold durations. Remaining unplanned modifier parameters are tracked as polish work.
- [x] Planned targets shown in the UI exactly match triggered targets; only loaded pads are eligible, and all pad-targeted modifiers treat selection/deselection as live input through the final pre-trigger moment. Loaded-pad or selection changes refresh the preview before firing. Swap Stack uses exactly the live user selection (minimum two), preserves its order, and never supplements it with scheduler-chosen pads; two stacks exchange and larger selections rotate without a promised destination map.
- [x] Audit downstream randomness. Musical structure and displayed variants are frozen; evolving S&H values, random slice choice/order, granular texture, and wow/flutter micro-parameters remain intentional internal motion. Legacy Ping Pong descriptors use a deterministic fallback rather than choosing a hidden trigger-time division.
- [x] Add unit tests for fill, trigger/pop, skip, force, suppression, seed reproducibility, settings changes, variant labels, and Switch Part destinations. The `LoopBreakerTests` CMake target runs them headlessly through CTest.

### 8.4 Modifier registry status

`ModifierRegistry` is now the canonical source for all 29 non-`Unknown`
entries. It owns ordered type, descriptor category, display and short names, UI
group, description, prototype eligibility, scheduler eligibility, probability
visibility, always-on state, and the representative reduced-motion glyph phase.

`ModifierProbabilityManager`, `ModifierFactory`, `ModifierScheduler`, Glyph Lab,
and the production glyph renderer now consume that registry. Serialized enum
values and established parameter/UI ordering are unchanged. Ducking is explicitly
represented as visible and always on, but neither prototyped nor scheduled.
Registry invariant tests guard complete/unique enum coverage and factory drift.

## 9. Glyph Lab decision

### 9.1 Decision

A **debug-build-only Glyph Lab tab** is implemented. This remains preferable to designing all graphics as generated images and handing them off later.

Reasons:

- The final assets should be JUCE paths so they scale cleanly and inherit theme colors.
- Timing, interpolation, reduced motion, compact readability, and parameter-driven variants require live evaluation.
- The application already conditionally adds a Debug tab under `JUCE_DEBUG`.
- `ModifierSelectionPanel` already enumerates modifiers and can inform the selector behavior.
- A shared renderer avoids drift between the lab and production Session UI.

Generated mockups remain art direction, not production assets.

### 9.2 Glyph Lab layout and current status

The debug-only top-level `Glyph Lab` tab is implemented. It uses the production renderer and currently provides modifier search, a live large preview, three compact previews, phase play/pause and scrubbing, a reduced-motion comparison, representative planned variants, and PNG contact-sheet export.

Left column:

- [x] Modifier list sourced from the current ordered modifier list.
- [x] Search/filter by name or category.
- [x] Representative planned variant data for parameter-sensitive glyphs.
- [ ] Editable variant presets and target-count controls.
- [ ] Local theme/background selector independent of the application Settings tab.

Center:

- [x] Large **NEXT** tile at production size.
- [x] Play/pause animation.
- [x] Scrubbable phase slider from 0–1.
- [ ] Optional bounds/debug-path overlay.
- [ ] Switchable light and black-aperture backgrounds.

Right:

- [x] Three adjacent compact queue cells to test rhythm and collisions.
- [x] 1× compact preview.
- [ ] Dedicated 2× compact preview.
- [x] Static reduced-motion frame.

Bottom:

- [x] Concise descriptor/category/phase text.
- [x] Export button for deterministic PNG contact sheets.
- [ ] Copyable stable preview configuration.
- [ ] Optional comparison strip showing the selected modifier beside neighboring glyphs.

### 9.3 Repository-visible review

Animations require the application, but static review should not.

The Glyph Lab now exports every modifier at fixed phases 0.0, 0.25, 0.5, and 0.75, plus a compact reduced-motion frame, into a deterministic PNG contact sheet. The user chooses the output path. Do not commit regenerated sheets by default; commit only curated design checkpoints under `docs/concepts/`.

This provides:

- fast visual regression review;
- artifacts other conversations can inspect without launching the plugin;
- consistent compact/large comparisons;
- a way to catch clipping and scaling issues.

The live Glyph Lab remains authoritative for motion.

### 9.4 Current implementation snapshot

Implemented on `codex/hieroglyph-ui-concepts` as of 2026-07-22:

- The `Control Surface (Light)` ThemeEngine palette is available and is the default for new sessions.
- `ControlSurfacePalette` maps application themes into stable semantic glyph colors.
- `ModifierGlyphRenderer` uses normalized JUCE vector geometry and dispatches every non-`Unknown` `ModifierType`.
- The Session NEXT tile uses the shared renderer, concise planned-variant text, the real scheduler countdown, a full-width progress rule, and no modifier-description prose.
- The scheduler owns a truthful depth-three planned queue with frozen descriptors and targets, copy snapshots, and a message-thread queue listener.
- The Session control board renders the real next item plus two static compact queue cells and concise family pips shared by NEXT and both queue cells.
- Debug modifier forcing can independently replace NEXT, QUEUE 1, or QUEUE 2 while preserving the other planned entries.
- Queue target planning is restricted to loaded pads and refreshes when pad availability or explicit selection changes, preventing visually valid queue entries from firing at empty pads.
- The wordmark itself carries coarse trigger progress; the redundant rule beneath it has been removed.
- The A–H preset strip uses flat ivory cells, occupancy lamps, and pending-recall underlines instead of broad filled/glowing buttons.
- Saved A–H preset slots retain a distinct vermilion-tinted fill in addition to their occupancy lamp; empty slots remain ivory.
- Pads now use ivory outer tiles and black waveform apertures; empty pads use four load brackets and a plus, while selection and playback use separate underline/lamp cues instead of full-perimeter glows.
- Production glyph motion is enabled by default at 15 Hz and completes one phase cycle per 4/4 bar at the current session/host BPM. Settings exposes `Animate modifier glyphs`; legacy sessions from the hidden/default-off era are migrated to motion-on once, while subsequent saves preserve the user's choice.
- Runtime status: the VST and three-item queue have been verified in-host. The loaded-pad targeting/application fix, Session geometry pass, occupied-preset treatment, and motion configuration all build and install successfully; final in-host modifier-application, animation, and visual confirmation are pending the next reload.
- Animation-disabled mode renders a deterministic representative frame as the reduced-motion option.
- The debug-only Glyph Lab and fixed-phase contact-sheet exporter are implemented.
- The Debug VST3 target builds successfully with the renderer and Glyph Lab.
- A CMake/CTest console runner now executes the Loop Breaker JUCE test category without a DAW or Xcode test-host scheme.
- NEXT and compact queue labels now share one structured variant formatter, covering Arp Slice, Slice Repeater, S&H filters, chorus, auto-pan, granular, filters, and explicit Switch Part destinations.
- Volume Ramp fade/hold timing is frozen during queue planning. Swap Stack snapshots the current ordered user selection and replans that snapshot whenever the selection changes, right up to trigger time; application never adds pads of its own.
- Master Low-Pass and Master High-Pass are retired from production scheduling and controls. Their enum IDs, registry entries, legacy application paths, glyphs, and APVTS parameter positions remain intact so existing sessions do not remap serialized state or automation.
- Speed, Stretch, Arp Slice, Auto-Pan, and Switch Part glyph geometry now responds to its frozen plan where that response remains legible at queue size.

Not yet implemented:

- production-size visual review and adjustment of the Session geometry;
- complete variant-to-geometry mapping, accessibility review, and performance validation.

## 10. Suggested implementation sequence

### Milestone 1 — Foundation

- [x] Introduce `ControlSurfacePalette` and the `Control Surface (Light)` ThemeEngine palette.
- [x] Introduce a canonical modifier metadata registry, including explicit always-on/non-scheduled status.
- [x] Add `ModifierGlyphRenderer` with normalized coordinates.
- [x] Implement large/compact preview containers and glyph dispatch.
- [x] Add the debug-only Glyph Lab tab.
- [x] Add fixed-phase PNG contact-sheet export.

### Milestone 2 — Core glyph family

- [x] Low-pass and high-pass first pass.
- [x] Delay, dub delay, and reverb first pass.
- [x] Reverse, speed, and stretch first pass.
- [x] Swap Stack and Reset All first pass.
- [ ] Validate compact recognition at the final production queue size. The production queue layout now exists; visual review remains.

### Milestone 3 — Queue backend

- [x] Replace single upcoming descriptor with planned queue.
- [x] Freeze prepared descriptors at planning time and target snapshots between loaded-pad/selection changes.
- [x] Add snapshot/listener APIs with message-thread queue delivery.
- [x] Update force/skip/reset/suppression semantics.
- [x] Add independent Debug-panel forcing for NEXT, QUEUE 1, and QUEUE 2.
- [x] Add scheduler queue and registry tests plus a runnable CMake/CTest console host.
- [x] Audit application-time randomness and remove preview/trigger mismatches for Switch Part, Volume Ramp, Swap Stack, and legacy Ping Pong fallback behavior.
- [x] Retire Master Low-Pass and Master High-Pass from scheduling and production controls while preserving their serialized enum IDs and legacy APVTS parameter positions.

### Milestone 4 — Session layout

- [ ] Implement light canvas and modular header. The palette, NEXT tile, and two truthful compact queue cells are complete; production-size visual review remains.
- [x] Integrate coarse timer behavior into the wordmark without adding a redundant logo rule.
- [x] Add large next tile and two truthful queue tiles with Probability-group family pips.
- [x] Restyle the A–H preset strip without changing its save/recall behavior.
- [x] Restyle loaded and empty pads without changing audio behavior: ivory tiles, black waveform apertures, and bracketed empty states are implemented.
- [x] Replace normal selection/playing borders with independent lamps and underlines. MIDI learn and file-drag modes retain dashed perimeters because the perimeter itself communicates those temporary modes.

### Milestone 5 — Remaining glyphs and polish

- [x] Complete an initial vector and motion pass for every glyph in Section 7.2.
- [ ] Map all meaningful planned variants into visuals and concise labels. Shared labels now cover every structured plan; glyph geometry responds to Speed, Stretch, Arp Slice, Auto-Pan, granular density, and Switch Part destination, with remaining geometry mappings still to review.
- [ ] Add mild pad-trigger overlays where useful.
- [ ] Tune performance at minimum and maximum editor sizes.
- [x] Add and visually expose reduced-motion mode through the production renderer and Glyph Lab.
- [ ] Validate color contrast and color-independent state cues.
- [ ] Produce and review final contact sheets. Export is implemented; final review and curated checkpoints remain.

### 10.1 Ordered next steps

1. **Review the production control board and first glyph pass.** Inspect the new NEXT/QUEUE 1/QUEUE 2 layout at minimum and maximum editor sizes, then export a current contact sheet and resolve clipping, centering, ambiguous motion, and color-only distinctions.
2. **Review and tune Session geometry and motion.** Reload the installed VST, verify saved-versus-empty A–H presets, the wordmark fill and modifier-board progress rule, animated NEXT glyphs at several speeds, the reduced-motion toggle, ivory/black pad treatment, filenames, and non-perimeter state cues, then correct any production-size spacing, contrast, or motion issues.
3. **Finish variant, accessibility, and performance work.** Map the remaining planned fields, verify reduced motion and color-independent state, test minimum/maximum editor sizes, and review queue planning/allocation cost before final contact-sheet approval.

## 11. Acceptance criteria

The redesign is successful when:

- A user can identify the next modifier primarily from its glyph and motion.
- A new user can confirm that interpretation from a short nearby name/value.
- Queue tiles remain distinguishable at their actual compact size.
- The large next tile and compact queue tiles clearly look like two states of the same glyph.
- Target pads are apparent without reading a sentence.
- The Session tab contains no modifier-description prose.
- Light surfaces, black apertures, fine rules, and sparse signal colors form a coherent system.
- Wavy/contour treatments communicate a relevant audio behavior and never become generic decoration.
- Disabling animation preserves all meaning.
- The scheduler previews exactly what will trigger, including variant and targets.
- UI rendering remains smooth and does not introduce audio-thread work.

## 12. Explicit non-goals

- Do not recreate a specific Alien or OP-1 screen.
- Do not introduce literal aliens, creatures, or narrative illustrations.
- Do not replace menu/settings language with unexplained symbols.
- Do not ship generated raster mockup artwork as production modifier icons.
- Do not redesign DSP behavior as part of the first visual milestone.
- Do not show a fake multi-item queue before the scheduler truly plans those items.
