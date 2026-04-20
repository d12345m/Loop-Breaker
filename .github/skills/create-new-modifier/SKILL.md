---
name: create-new-modifier
description: "Add a new modifier to the Loop Breaker audio plugin. Use when: creating a new effect modifier, adding a new buffer transform, implementing a new channel effect, adding a new master effect, or adding a new global utility modifier. Covers enum registration, factory, probability panel, variant randomization, queue display, preset serialization, reset cleanup, manual documentation, and AppState dispatch."
argument-hint: "Describe the new modifier: name, category, what it does, and any variant parameters"
---

# Create New Modifier

## When to Use

- Adding a new audio effect modifier to Loop Breaker
- Implementing a new buffer transform, channel effect, master effect, or global utility
- User says "add modifier", "new effect", "create modifier", or similar

## Clarification Questions

Before starting, ask the user these questions if not already answered:

1. **Permanent or temporary?**
   - **Permanent**: Modifies buffer state until Reset clears it (e.g., Reverse, Speed, BeatSlice)
   - **Temporary**: Applies an envelope that auto-decays back to original state (e.g., Master LPF, Volume Ramp)

2. **Should the modifier state be saved in the preset system?**
   - If yes: add fields to `BufferModifierSnapshot` with serialization
   - If no: the modifier fires and forgets (or is purely temporary with envelopes)

3. **Category**: BufferTransform, BufferEffect, MasterEffect, or GlobalUtility?

4. **Variant parameters**: What should be randomized? (e.g., speed ratios, wet/dry, duration, division)

## Procedure

Follow these 15 steps in order. Each step references exact file locations.

### Step 1: Add Enum Value

**File**: [Source/Modifier.h](./references/modifier-locations.md) — `enum class ModifierType`

Add the new type in the appropriate category section of the enum (before `Unknown`):

```cpp
enum class ModifierType
{
    // Buffer modifiers
    Reverse,
    Speed,
    // ... existing types ...

    // Add your new type in its category group:
    YourNewModifier,    // <-- add here

    // Global
    ResetAll,
    Unknown
};
```

### Step 2: Add Planned Variant Fields to ModifierDescriptor

**File**: `Source/Modifier.h` — `struct ModifierDescriptor`

If the modifier has randomizable parameters, add `std::optional<T>` fields:

```cpp
// YourNewModifier parameters
std::optional<double> plannedYourParam;      // description of what it controls
std::optional<int>    plannedYourOtherParam;  // description
```

Follow existing naming: prefix with `planned`, use camelCase.

### Step 3: Register Factory Prototype

**File**: `Source/Modifier.cpp` — `ModifierFactory::createAllPrototypes()`

Add an `add()` call in the appropriate category section:

```cpp
add(ModifierType::YourNewModifier, ModifierCategory::BufferEffect,
    "Your Display Name", "Short description of what it does");
```

### Step 4: Create Concrete Modifier Class (if needed)

**File**: `Source/Modifier.cpp` — in the anonymous namespace at top

Only needed if the modifier has custom `begin()` logic. Otherwise `SimpleModifierBase` is used.

```cpp
class YourNewModifier : public SimpleModifierBase
{
public:
    YourNewModifier() : SimpleModifierBase({
        ModifierType::YourNewModifier,
        ModifierCategory::BufferEffect,
        "Your Display Name",
        "Description"
    }) {}
    bool begin(const ModifierContext& ctx) override;
};
```

Then add a case in `ModifierFactory::createInstance()`:

```cpp
case ModifierType::YourNewModifier: return std::make_unique<YourNewModifier>();
```

### Step 5: Add to Probability System

**File**: `Source/ModifierProbabilityManager.h` — three locations:

**A.** `getDisplayName()` — add switch case:

```cpp
case ModifierType::YourNewModifier: return "Your Display Name";
```

**B.** `getCategory()` — add to the appropriate category group:

```cpp
case ModifierType::YourNewModifier:  // falls through with other BufferEffect types
```

**C.** `allModifierTypes()` — add to the static vector in the correct category section:

```cpp
ModifierType::YourNewModifier,
```

> The probability panel UI (`ModifierProbabilityPanel.h`) iterates `allModifierTypes()` automatically — no changes needed there. The APVTS parameter `"prob_N"` (where N = enum int value) is auto-generated.

### Step 6: Add to Debug Panel

**File**: `Source/ModifierSelectionPanel.h` — constructor

This panel lists all modifiers as toggle buttons for force-triggering during development. Add an `addToggle()` call in the appropriate category group:

```cpp
addToggle(ModifierType::YourNewModifier, "Your Display Name");
```

Place it near other modifiers of the same category. The panel auto-layouts vertically.

### Step 7: Add to Scheduler Whitelist

**File**: `Source/ModifierScheduler.cpp` — `pickRandomDescriptor()` function

The scheduler has a `restrictToImplemented` flag (default: `true`) that limits random selection to an explicit whitelist of modifier types. **If you skip this step, your modifier will never be randomly selected — it will only fire via the debug panel or force-trigger.**

Find the `if (restrictToImplemented.load())` block and add your type to the `bool allowed = (...)` expression:

```cpp
if (restrictToImplemented.load())
{
    for (int i = 0; i < prototypeCache.size(); ++i)
    {
        auto t = prototypeCache[i]->getDescriptor().type;
        bool allowed = (t == ModifierType::Reverse
            || t == ModifierType::Speed
            // ... existing types ...
            || t == ModifierType::YourNewModifier);  // <-- ADD HERE
        if (!allowed) continue;
        candidateIndices.add(i);
    }
}
```

Place your entry near other modifiers of the same category within the condition chain. Without this, the probability panel and APVTS parameter will exist but have no effect — the modifier simply won't appear as a candidate for random selection.

### Step 8: Add Variant Randomization

**File**: `Source/ModifierScheduler.cpp` — `prepareVariantDescriptor()` function

Add an `else if` block for your modifier type:

```cpp
else if (base.type == ModifierType::YourNewModifier)
{
    // Define option arrays
    static const double options[] { 0.25, 0.5, 0.75, 1.0 };
    double chosen = options[rng.nextInt((int)std::size(options))];

    // Populate planned fields
    modified.plannedYourParam = chosen;

    // Build description suffix
    modified.description = base.description + " -> "
        + juce::String(chosen * 100.0, 0) + "%";
}
```

**Randomization helpers available**: `rng.nextInt(max)`, `rng.nextFloat()`, `rng.nextBool()`, `rng.nextDouble()`

### Step 9: Add Queue Display

**File**: `Source/UpcomingModifierDisplay.h` — `setUpcoming()` method

Add a conditional block that checks your planned fields and builds a variant string:

```cpp
if (d.plannedYourParam.has_value())
{
    variantText = juce::String(*d.plannedYourParam * 100.0, 0) + "%";
    if (d.plannedFxFadeBars.has_value())
        variantText += " | Fade: " + (*d.plannedFxFadeBars == 0.0
            ? juce::String("instant")
            : juce::String(*d.plannedFxFadeBars, 0) + " bars");
}
```

### Step 10: Implement Modifier Application in AppState

**File**: `Source/AppState.h` — `modifierTriggered()` switch statement

**A.** Add a case to the dispatch switch:

```cpp
case ModifierType::YourNewModifier:
    applyYourNewModifier(desc, targets);
    break;
```

**B.** Implement the application method:

For **permanent** modifiers (state persists until Reset):

```cpp
void applyYourNewModifier(const ModifierDescriptor& desc, const juce::Array<int>& targets)
{
    if (targets.isEmpty()) return;
    double param = desc.plannedYourParam.value_or(0.5);
    for (int idx : targets)
    {
        if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
        {
            b->setYourParam(param);
            if (!b->isPlaying()) b->play();
        }
    }
}
```

For **temporary** modifiers (auto-decay via envelope):

```cpp
void applyYourNewModifier(const ModifierDescriptor& desc, const juce::Array<int>& targets)
{
    if (targets.isEmpty()) return;
    double bars = desc.plannedFxFadeBars.value_or(2.0);
    float target = (float)desc.plannedYourParam.value_or(0.75);
    for (int idx : targets)
    {
        if (juce::isPositiveAndBelow(idx, channelStrips.size()))
        {
            auto& strip = *channelStrips[idx];
            strip.effects().yourEffectEnabled = true;
            float startVal = strip.getFxParams().yourParam;
            strip.startTemporaryYourEffect(target, (float)(bars * 0.5), startVal, (float)(bars * 0.5));
        }
    }
}
```

### Step 11: Ensure Reset Cleans Up the Modifier

The Reset modifier must undo your new modifier's effects. Check these two reset paths:

**A. `ChannelStrip::reset()`** in `Source/ChannelStrip.h`:

- If you added FX enable flags to `EffectEnables`, ensure `EffectEnables::reset()` clears them (line ~30: the chained assignment sets all flags to `false`)
- If you added envelope fields, ensure they are reset to `{}` in `ChannelStrip::reset()` (line ~597)
- If you added FX parameters to `FxParams`, they auto-reset via `params = FxParams{}` (default construction)

**B. `AudioBufferPlayer::resetToDefaults()`** in `Source/AudioBufferPlayer.h` (or similar):

- If your modifier changes buffer playback state (speed, slicing, pitch, etc.), ensure `resetToDefaults()` reverts it

**C. `applyReset()` in `Source/AppState.h`** (line ~587):

- The existing implementation calls `channelStrips[idx]->reset()` which resets both the strip and its underlying buffer
- If your modifier has additional state outside ChannelStrip/AudioBufferPlayer (rare), add cleanup here

**Verification**: After implementing, trigger your modifier then trigger Reset — confirm all state returns to defaults.

### Step 12: Add to Preset System (if applicable)

Only if the user confirmed the modifier state should be saveable in presets.

**File**: `Source/ModifierPreset.h` — `struct BufferModifierSnapshot`

**A.** Add fields:

```cpp
bool yourEffectEnabled = false;
float yourParam = 0.5f;  // default value
```

**B.** Add to `toVar()` serialization (use short abbreviated keys):

```cpp
obj->setProperty("yrEn", (bool)yourEffectEnabled);
obj->setProperty("yrPrm", (double)yourParam);
```

**C.** Add to `fromVar()` deserialization (with default fallbacks for backward compatibility):

```cpp
s.yourEffectEnabled = getB("yrEn", false);
s.yourParam = getF("yrPrm", 0.5f);
```

> Backward compatibility is automatic — `fromVar()` helper functions return defaults when keys are missing from old saves.

### Step 13: Add History Panel Color (optional)

**File**: `Source/ModifierHistoryPanel.h` — `paintListBoxItem()` method

The history panel shows triggered modifiers with color-coded rows. By default, new modifiers use `Theme::textSubtle()`. To assign a distinct color, add a case to the `switch (e.type)` block:

```cpp
case ModifierType::YourNewModifier: typeColour = Theme::accent(); break;
```

Available theme colors: `Theme::accent()`, `Theme::accent2()`, `Theme::warn()`, `Theme::good()`, `Theme::textSubtle()`.

This step is optional — modifiers work fine with the default subtle color.

### Step 14: Add to Manual

**File**: `generate_manual.py` — modifier documentation lists (around line 969-1081)

Add a tuple to the appropriate list:

```python
# In the appropriate category list (buffer_mods, channel_mods, master_mods, special_mods):
("Your Modifier Name", "Description of what it does. Variant options include X, Y, Z."),
```

The PDF rendering iterates the list automatically — no other changes needed.

### Step 15: Build and Validate

1. Build both targets: VST3 and AU (Debug)
2. Run pluginval to verify no crashes
3. Test in DAW: verify modifier appears in probability panel, triggers correctly, displays in queue, and resets properly

## File Reference

| File                                  | What to Change                                                                        |
| ------------------------------------- | ------------------------------------------------------------------------------------- |
| `Source/Modifier.h`                   | Enum value + planned variant fields in `ModifierDescriptor`                           |
| `Source/Modifier.cpp`                 | Factory prototype + concrete class (optional) + `createInstance()` case               |
| `Source/ModifierProbabilityManager.h` | `getDisplayName()` + `getCategory()` + `allModifierTypes()`                           |
| `Source/ModifierSelectionPanel.h`     | `addToggle()` call in constructor for debug panel                                     |
| `Source/ModifierScheduler.cpp`        | `pickRandomDescriptor()` whitelist + `prepareVariantDescriptor()` randomization block |
| `Source/UpcomingModifierDisplay.h`    | `setUpcoming()` variant text display                                                  |
| `Source/AppState.h`                   | `modifierTriggered()` switch case + `applyYourModifier()` method                      |
| `Source/ChannelStrip.h`               | FX enables, params, envelopes, reset (if channel effect)                              |
| `Source/ModifierPreset.h`             | `BufferModifierSnapshot` fields + `toVar()`/`fromVar()` (if preset-saved)             |
| `Source/ModifierHistoryPanel.h`       | Optional color case in `paintListBoxItem()`                                           |
| `generate_manual.py`                  | Modifier description tuple in appropriate category list                               |

## Categories Quick Reference

| Category        | Enum                                | Targets                | Examples                            |
| --------------- | ----------------------------------- | ---------------------- | ----------------------------------- |
| BufferTransform | `ModifierCategory::BufferTransform` | Per-pad buffers        | Reverse, Speed, BeatSlice, PingPong |
| BufferEffect    | `ModifierCategory::BufferEffect`    | Per-pad channel strips | Delay, Reverb, Chorus, Auto-Pan     |
| MasterEffect    | `ModifierCategory::MasterEffect`    | All channel strips     | Master HPF, Master LPF              |
| GlobalUtility   | `ModifierCategory::GlobalUtility`   | System-wide            | SwitchPart, QuarterNoteBurst        |
