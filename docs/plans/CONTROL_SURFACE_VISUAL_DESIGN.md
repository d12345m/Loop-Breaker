# Loop Breaker — Control Surface Visual Design Brief

- Status: implementation in progress; visual foundation and first glyph pass complete
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

The proposed combination is:

- Keep the countdown/progress behavior integrated with the wordmark.
- Keep a short horizontal line below the wordmark as a stable brand mark.
- Let the line also act as a coarse progress indicator: a colored segment traverses or fills it as the trigger approaches.
- The detailed countdown still belongs with the large “next” modifier glyph.
- Timer motion must not distort or reduce the legibility of the logo.

### 5.3 Modifier control board

The center header becomes a modular ivory control board:

- One large **next** cell, approximately twice the width of each queue cell.
- Two or three smaller **queued** cells.
- The large cell contains the main glyph, `NEXT`, the modifier name, planned variant details, and target pips.
- Queue cells contain a compact glyph, short name, and optionally one essential variant value.
- The master-volume control remains at the far right.
- No descriptive modifier sentence appears here.

### 5.4 Target strip

- Preserve A–H as a single horizontal strip aligned to the eight pads.
- Selected targets use a short colored underline and/or a small circular lamp.
- Avoid a full bright border around the target selector.
- The large next cell repeats the selected target colors as small pips so the relationship is visible without routing lines.

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
- Respect the existing animation enable/speed configuration.
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
| Buffer Volume Ramp Down | Bespectacled librarian with a bun shushing a talker | The talker's voice marks contract and disappear while the raised finger remains fixed | Librarian profile, raised finger, and diminishing voice marks | Fade bars if present |
| Buffer Tremolo | Stable center line wrapped by alternating amplitude lobes | Lobes pulse symmetrically around the center | Alternating high/low marks | Rate/depth if later planned |
| Buffer Chorus On | One central trace with two slightly offset wavy companions | Companion traces drift in and out of phase | Three offset waves | Rate, depth, mix |
| Buffer Auto Pan | Signal dot orbiting between L and R half-circles | Dot sweeps laterally; depth controls travel range | L/R arc with dot | Rate, depth, mix |
| Buffer Ducking On | Large foreground pulse pressing a second signal below a threshold | Secondary line dips whenever foreground pulse arrives | Pulse over downward notch | None currently; ducking remains always-on/non-prototyped |
| Buffer S&H Low Pass On | One irregular held-cutoff trace crossed by a descending filter slash | A single random-looking pattern regenerates only at discrete hold intervals | One stepped trace + low-pass slash | S&H division |
| Buffer S&H High Pass On | One irregular held-cutoff trace crossed by an ascending filter slash | A single random-looking pattern regenerates only at discrete hold intervals | One stepped trace + high-pass slash | S&H division |
| Buffer Granular On | Cloud of small grains distributed around a central waveform seed | Grains appear, drift, and recombine continuously | Sparse dot cloud | Density, size, pitch spread, mix, fade |
| Buffer Granular Momentary | Compact grain packet that erupts and collapses | Short outward grain burst, then snap back | Dense dot packet + burst ticks | Density, size, pitch spread, mix |
| Master High Pass On | High-pass glyph spanning all eight target ticks | Sweep crosses a row of eight small channel marks | High-pass slash + `ALL`/eight dots | Fade bars / immediate-jump mode |
| Master Low Pass On | Low-pass glyph spanning all eight target ticks | Sweep crosses a row of eight small channel marks | Low-pass slash + `ALL`/eight dots | Fade bars / immediate-jump mode |
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

### 8.1 Current limitation

`ModifierScheduler` currently owns one value:

```cpp
std::optional<ModifierDescriptor> upcoming;
```

and exposes `getUpcomingModifier()`. `ModifierSchedulerListener` broadcasts only one descriptor through `upcomingModifierChanged`. Therefore a two- or three-item visual queue cannot yet be truthful.

### 8.2 Desired model

The scheduler should own a planned queue, suggested default depth **3**:

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

The descriptor must be fully variant-planned when it enters the queue. If targets are intended to remain user-reactive until trigger time, document that rule explicitly; otherwise targets should also be frozen at enqueue time so the preview remains truthful. The preferred UX is to freeze both descriptor and targets.

Suggested public surface:

```cpp
std::vector<PlannedModifier> getPlannedQueueSnapshot() const;
```

Suggested listener callback:

```cpp
virtual void plannedQueueChanged(const std::vector<PlannedModifier>&) {}
```

Use a snapshot/copy suitable for the message thread. Do not let the UI hold references into scheduler-owned storage. Review synchronization carefully because scheduler updates may originate from audio/host-timeline processing while UI reads occur on the message thread.

### 8.3 Queue behavior checklist

- [ ] Fill queue to depth 3 on scheduler start.
- [ ] Front item is the next modifier and owns the active countdown.
- [ ] On trigger, pop front and append one newly planned item.
- [ ] `skipUpcoming()` pops front without firing, then replenishes.
- [ ] `forceUpcomingModifier()` replaces only the front item unless an explicit queue-slot API is added.
- [ ] Debug tooling can set each queue position independently.
- [ ] Probability/settings changes affect newly enqueued items, not already-previewed entries.
- [ ] Suppression advances the queue consistently with current trigger semantics.
- [ ] Reset/start/stop behavior is deterministic and tested.
- [ ] Seeded randomness produces reproducible full queues.
- [ ] Queue never contains `Unknown`.
- [ ] Planned variants shown in the UI exactly match triggered variants.
- [ ] Planned targets shown in the UI exactly match triggered targets.
- [ ] Add unit tests for fill, trigger/pop, skip, force, suppression, seed reproducibility, and settings changes.

### 8.4 Modifier registry status

The immediate count mismatch has been corrected:

- `ModifierType` includes `BufferDuckingOn`.
- `ModifierProbabilityManager::allModifierTypes()` includes all 29 non-`Unknown` enum entries.
- `SessionSettings::kNumModifierTypes` now derives from the terminal `ModifierType::Unknown` sentinel instead of a hard-coded value.
- The existing processor assertion still checks the ordered-list size against that derived count.

The canonical-registry task is not complete. Display names, categories, prototype descriptors, and scheduler eligibility are still maintained in more than one place. `ModifierFactory::createAllPrototypes()` also intentionally omits `BufferDuckingOn` because ducking is currently always on; a future registry must represent that distinction explicitly instead of relying on parallel lists and comments.

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
- Animation-disabled mode renders a deterministic representative frame.
- The debug-only Glyph Lab and fixed-phase contact-sheet exporter are implemented.
- The Debug VST3 target builds successfully with the renderer and Glyph Lab.

Not yet implemented:

- a canonical modifier metadata/eligibility registry;
- a truthful multi-item planned queue with frozen variants and targets;
- production queue cells and target pips in the Session header;
- the final A–H target-strip and pad restyling;
- complete variant-to-geometry mapping, accessibility review, and performance validation.

## 10. Suggested implementation sequence

### Milestone 1 — Foundation

- [x] Introduce `ControlSurfacePalette` and the `Control Surface (Light)` ThemeEngine palette.
- [ ] Introduce a canonical modifier metadata registry. The count guard is fixed, but metadata and eligibility remain duplicated.
- [x] Add `ModifierGlyphRenderer` with normalized coordinates.
- [x] Implement large/compact preview containers and glyph dispatch.
- [x] Add the debug-only Glyph Lab tab.
- [x] Add fixed-phase PNG contact-sheet export.

### Milestone 2 — Core glyph family

- [x] Low-pass and high-pass first pass.
- [x] Delay, dub delay, and reverb first pass.
- [x] Reverse, speed, and stretch first pass.
- [x] Swap Stack and Reset All first pass.
- [ ] Validate compact recognition at the final production queue size. Glyph Lab previews exist, but the production queue layout does not.

### Milestone 3 — Queue backend

- [ ] Replace single upcoming descriptor with planned queue.
- [ ] Freeze variants and targets at planning time.
- [ ] Add snapshot/listener APIs.
- [ ] Update force/skip/reset/suppression semantics.
- [ ] Add scheduler tests.

### Milestone 4 — Session layout

- [ ] Implement light canvas and modular header. The palette and large NEXT tile are complete; the full control board is not.
- [ ] Combine logo line and timer behavior.
- [ ] Add large next tile and 2–3 queue tiles. The large tile is complete; queue tiles wait on Milestone 3.
- [ ] Restyle A–H targeting strip.
- [ ] Restyle loaded and empty pads without changing audio behavior. Theme colors have changed, but final aperture/load/label geometry remains.
- [ ] Replace full selection borders with lamps/underlines/corner tabs.

### Milestone 5 — Remaining glyphs and polish

- [x] Complete an initial vector and motion pass for every glyph in Section 7.2.
- [ ] Map all meaningful planned variants into visuals and concise labels. Slice and granular parameters are partially mapped.
- [ ] Add mild pad-trigger overlays where useful.
- [ ] Tune performance at minimum and maximum editor sizes.
- [x] Add and visually expose reduced-motion mode through the production renderer and Glyph Lab.
- [ ] Validate color contrast and color-independent state cues.
- [ ] Produce and review final contact sheets. Export is implemented; final review and curated checkpoints remain.

### 10.1 Ordered next steps

1. **Review the first glyph pass.** Export a current contact sheet, inspect all four phases plus compact frames, and resolve clipping, centering, ambiguous motion, and color-only distinctions. Record durable glyph changes in Section 7.2.
2. **Create the canonical modifier registry.** Consolidate ordered type, display name, category, prototype/scheduler eligibility, and default glyph metadata. Preserve serialized enum values and explicitly represent always-on/non-scheduled entries such as ducking.
3. **Implement the planned queue backend.** Follow Section 8 exactly: depth three, fully planned descriptors, frozen targets, copy snapshots, message-thread-safe listener delivery, deterministic seed behavior, and comprehensive scheduler tests.
4. **Build the production control board.** Use the queue snapshot for one large NEXT tile and two compact queued tiles. Add real target pips only after targets are frozen; do not infer or fake them.
5. **Complete Session geometry.** Add the stable logo progress rule, restyle the A–H target strip, then implement ivory pad tiles with black waveform apertures and non-perimeter selection cues.
6. **Finish variant, accessibility, and performance work.** Map the remaining planned fields, verify reduced motion and color-independent state, test minimum/maximum editor sizes, and review UI/audio-thread cost before final contact-sheet approval.

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
