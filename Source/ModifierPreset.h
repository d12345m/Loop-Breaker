/*
 ==============================================================================
   ModifierPreset.h
   --------------------------------------------------------------------------
   Snapshot structs for saving and recalling modifier state across all 8 buffers.
   Each preset captures per-buffer AudioBuffer transform state and ChannelStrip
   FX state.  Parts settings are explicitly excluded.
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>

/**
    Snapshot of a single buffer's modifier-affected state.
    Captures AudioBuffer transform params + ChannelStrip FX enables + FxParams.
*/
struct BufferModifierSnapshot
{
    // ── AudioBuffer transform state ──
    double speed               = 1.0;
    double stretchRatio        = 1.0;
    double pitchSemiTones      = 0.0;
    bool   continuousRandomSlicing = false;
    int    numSlices           = 1;
    bool   pingPongEnabled     = false;
    double pingPongDivision    = 0.25;
    bool   arpSliceActive      = false;
    bool   arpSliceRepeaterMode = false;
    int    arpSequenceLength   = 4;
    int    arpRepeatBars       = 2;
    int    arpTotalSlices      = 16;

    // ── ChannelStrip FX enable flags (EffectChainPlaceholder) ──
    bool delayEnabled          = false;
    bool reverbEnabled         = false;
    bool lowPassEnabled        = false;
    bool highPassEnabled       = false;
    bool tremoloEnabled        = false;
    bool chorusEnabled         = false;
    bool autoPanEnabled        = false;
    bool volumeRampEnabled     = false;
    bool shLowPassEnabled      = false;
    bool shHighPassEnabled     = false;

    // ── ChannelStrip FxParams ──
    float reverbWet            = 0.0f;
    float reverbPreDelayMs     = 0.0f;
    float delayFeedback        = 0.0f;
    float delayTimeMs          = 400.0f;
    float delayWet             = 0.35f;
    bool  delayPingPong        = false;
    float delayFeedbackHighCutHz = 6000.0f;
    float delayFbDrive         = 1.0f;
    bool  duckingEnabled       = true;
    float duckAmount           = 0.5f;
    float duckReleaseMs        = 250.0f;
    bool  wowFlutterEnabled    = false;
    float wowDepthMs           = 3.0f;
    float wowRateHz            = 0.35f;
    float flutterDepthMs       = 0.8f;
    float flutterRateHz        = 6.0f;
    float wowPeriodBars        = 0.0f;
    float flutterPeriodBars    = 0.0f;
    float lowPassCutoff        = 20000.0f;
    float highPassCutoff       = 20.0f;
    float tremoloDepth         = 0.0f;
    float tremoloRateHz        = 4.0f;
    float chorusDepth          = 0.5f;
    float chorusRateHz         = 1.0f;
    float chorusMix            = 0.5f;
    float chorusDelayMs        = 7.0f;
    float panRateHz            = 2.0f;
    float panDepth             = 1.0f;
    float panMix               = 0.5f;
    float panPeriodBars        = 0.0f;
    float volumeGain           = 1.0f;
    float shLpfCutoff          = 4000.0f;
    float shLpfQ               = 1.0f;
    float shHpfCutoff          = 200.0f;
    float shHpfQ               = 1.0f;
    float shDivisionBars       = 0.25f;

    // ── Serialization ──
    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("speed",        speed);
        o->setProperty("stretch",      stretchRatio);
        o->setProperty("pitch",        pitchSemiTones);
        o->setProperty("slicing",      continuousRandomSlicing);
        o->setProperty("numSlices",    numSlices);
        o->setProperty("ppOn",         pingPongEnabled);
        o->setProperty("ppDiv",        pingPongDivision);
        o->setProperty("arpOn",        arpSliceActive);
        o->setProperty("arpRep",       arpSliceRepeaterMode);
        o->setProperty("arpSeqLen",    arpSequenceLength);
        o->setProperty("arpRepBars",   arpRepeatBars);
        o->setProperty("arpTotal",     arpTotalSlices);

        o->setProperty("fxDelay",      delayEnabled);
        o->setProperty("fxReverb",     reverbEnabled);
        o->setProperty("fxLpf",        lowPassEnabled);
        o->setProperty("fxHpf",        highPassEnabled);
        o->setProperty("fxTrem",       tremoloEnabled);
        o->setProperty("fxChorus",     chorusEnabled);
        o->setProperty("fxPan",        autoPanEnabled);
        o->setProperty("fxVolRamp",    volumeRampEnabled);
        o->setProperty("fxShLpf",      shLowPassEnabled);
        o->setProperty("fxShHpf",      shHighPassEnabled);

        o->setProperty("revWet",       (double) reverbWet);
        o->setProperty("revPdMs",      (double) reverbPreDelayMs);
        o->setProperty("dlyFb",        (double) delayFeedback);
        o->setProperty("dlyMs",        (double) delayTimeMs);
        o->setProperty("dlyWet",       (double) delayWet);
        o->setProperty("dlyPP",        delayPingPong);
        o->setProperty("dlyHiCut",     (double) delayFeedbackHighCutHz);
        o->setProperty("dlyDrv",       (double) delayFbDrive);
        o->setProperty("duckOn",       duckingEnabled);
        o->setProperty("duckAmt",      (double) duckAmount);
        o->setProperty("duckRel",      (double) duckReleaseMs);
        o->setProperty("wfOn",         wowFlutterEnabled);
        o->setProperty("wowDMs",       (double) wowDepthMs);
        o->setProperty("wowHz",        (double) wowRateHz);
        o->setProperty("fluDMs",       (double) flutterDepthMs);
        o->setProperty("fluHz",        (double) flutterRateHz);
        o->setProperty("wowBars",      (double) wowPeriodBars);
        o->setProperty("fluBars",      (double) flutterPeriodBars);
        o->setProperty("lpfCut",       (double) lowPassCutoff);
        o->setProperty("hpfCut",       (double) highPassCutoff);
        o->setProperty("tremD",        (double) tremoloDepth);
        o->setProperty("tremHz",       (double) tremoloRateHz);
        o->setProperty("chDep",        (double) chorusDepth);
        o->setProperty("chHz",         (double) chorusRateHz);
        o->setProperty("chMix",        (double) chorusMix);
        o->setProperty("chDlyMs",      (double) chorusDelayMs);
        o->setProperty("panHz",        (double) panRateHz);
        o->setProperty("panDep",       (double) panDepth);
        o->setProperty("panMix",       (double) panMix);
        o->setProperty("panBars",      (double) panPeriodBars);
        o->setProperty("volGain",      (double) volumeGain);
        o->setProperty("shLpfCut",     (double) shLpfCutoff);
        o->setProperty("shLpfQ",       (double) shLpfQ);
        o->setProperty("shHpfCut",     (double) shHpfCutoff);
        o->setProperty("shHpfQ",       (double) shHpfQ);
        o->setProperty("shDivBars",    (double) shDivisionBars);

        return juce::var(o);
    }

    static BufferModifierSnapshot fromVar(const juce::var& v)
    {
        BufferModifierSnapshot s;
        if (! v.isObject()) return s;

        auto* o = v.getDynamicObject();
        if (o == nullptr) return s;

        auto getD = [&](const char* key, double def) { return o->hasProperty(key) ? (double) o->getProperty(key) : def; };
        auto getF = [&](const char* key, float def)  { return o->hasProperty(key) ? (float)(double) o->getProperty(key) : def; };
        auto getB = [&](const char* key, bool def)   { return o->hasProperty(key) ? (bool) o->getProperty(key) : def; };

        s.speed                    = getD("speed",    1.0);
        s.stretchRatio             = getD("stretch",  1.0);
        s.pitchSemiTones           = getD("pitch",    0.0);
        s.continuousRandomSlicing  = getB("slicing",  false);
        s.numSlices                = (int) getD("numSlices", 1.0);
        s.pingPongEnabled          = getB("ppOn",     false);
        s.pingPongDivision         = getD("ppDiv",    0.25);
        s.arpSliceActive           = getB("arpOn",    false);
        s.arpSliceRepeaterMode     = getB("arpRep",   false);
        s.arpSequenceLength        = (int) getD("arpSeqLen",  4.0);
        s.arpRepeatBars            = (int) getD("arpRepBars", 2.0);
        s.arpTotalSlices           = (int) getD("arpTotal",   16.0);

        s.delayEnabled             = getB("fxDelay",  false);
        s.reverbEnabled            = getB("fxReverb", false);
        s.lowPassEnabled           = getB("fxLpf",    false);
        s.highPassEnabled          = getB("fxHpf",    false);
        s.tremoloEnabled           = getB("fxTrem",   false);
        s.chorusEnabled            = getB("fxChorus", false);
        s.autoPanEnabled           = getB("fxPan",    false);
        s.volumeRampEnabled        = getB("fxVolRamp",false);
        s.shLowPassEnabled         = getB("fxShLpf",  false);
        s.shHighPassEnabled        = getB("fxShHpf",  false);

        s.reverbWet                = getF("revWet",   0.0f);
        s.reverbPreDelayMs         = getF("revPdMs",  0.0f);
        s.delayFeedback            = getF("dlyFb",    0.0f);
        s.delayTimeMs              = getF("dlyMs",    400.0f);
        s.delayWet                 = getF("dlyWet",   0.35f);
        s.delayPingPong            = getB("dlyPP",    false);
        s.delayFeedbackHighCutHz   = getF("dlyHiCut", 6000.0f);
        s.delayFbDrive             = getF("dlyDrv",   1.0f);
        s.duckingEnabled           = getB("duckOn",   true);
        s.duckAmount               = getF("duckAmt",  0.5f);
        s.duckReleaseMs            = getF("duckRel",  250.0f);
        s.wowFlutterEnabled        = getB("wfOn",     false);
        s.wowDepthMs               = getF("wowDMs",   3.0f);
        s.wowRateHz                = getF("wowHz",    0.35f);
        s.flutterDepthMs           = getF("fluDMs",   0.8f);
        s.flutterRateHz            = getF("fluHz",    6.0f);
        s.wowPeriodBars            = getF("wowBars",  0.0f);
        s.flutterPeriodBars        = getF("fluBars",  0.0f);
        s.lowPassCutoff            = getF("lpfCut",   20000.0f);
        s.highPassCutoff           = getF("hpfCut",   20.0f);
        s.tremoloDepth             = getF("tremD",    0.0f);
        s.tremoloRateHz            = getF("tremHz",   4.0f);
        s.chorusDepth              = getF("chDep",    0.5f);
        s.chorusRateHz             = getF("chHz",     1.0f);
        s.chorusMix                = getF("chMix",    0.5f);
        s.chorusDelayMs            = getF("chDlyMs",  7.0f);
        s.panRateHz                = getF("panHz",    2.0f);
        s.panDepth                 = getF("panDep",   1.0f);
        s.panMix                   = getF("panMix",   0.5f);
        s.panPeriodBars            = getF("panBars",  0.0f);
        s.volumeGain               = getF("volGain",  1.0f);
        s.shLpfCutoff              = getF("shLpfCut", 4000.0f);
        s.shLpfQ                   = getF("shLpfQ",   1.0f);
        s.shHpfCutoff              = getF("shHpfCut", 200.0f);
        s.shHpfQ                   = getF("shHpfQ",   1.0f);
        s.shDivisionBars           = getF("shDivBars",0.25f);

        return s;
    }
};

/**
    A single preset slot.  May be empty (occupied == false) or hold a full snapshot
    of all 8 buffers' modifier states.
*/
struct ModifierPresetSlot
{
    bool occupied = false;
    std::array<BufferModifierSnapshot, 8> buffers;

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("occupied", occupied);

        juce::Array<juce::var> bufs;
        bufs.ensureStorageAllocated(8);
        for (int i = 0; i < 8; ++i)
            bufs.add(buffers[static_cast<size_t>(i)].toVar());
        o->setProperty("buffers", juce::var(bufs));

        return juce::var(o);
    }

    static ModifierPresetSlot fromVar(const juce::var& v)
    {
        ModifierPresetSlot slot;
        if (! v.isObject()) return slot;

        auto* o = v.getDynamicObject();
        if (o == nullptr) return slot;

        slot.occupied = o->hasProperty("occupied") ? (bool) o->getProperty("occupied") : false;

        if (o->hasProperty("buffers"))
        {
            auto bufsVar = o->getProperty("buffers");
            if (bufsVar.isArray())
            {
                auto* arr = bufsVar.getArray();
                const int n = juce::jmin((int) arr->size(), 8);
                for (int i = 0; i < n; ++i)
                    slot.buffers[static_cast<size_t>(i)] = BufferModifierSnapshot::fromVar(arr->getReference(i));
            }
        }

        return slot;
    }
};

/**
    Bank of 4 modifier state presets (A–D).
    Provides capture/restore and full JSON serialization.
*/
struct ModifierPresetBank
{
    static constexpr int kNumPresets = 4;

    std::array<ModifierPresetSlot, kNumPresets> slots;

    bool isSlotOccupied(int index) const
    {
        if (index < 0 || index >= kNumPresets) return false;
        return slots[static_cast<size_t>(index)].occupied;
    }

    void clearSlot(int index)
    {
        if (index < 0 || index >= kNumPresets) return;
        slots[static_cast<size_t>(index)] = ModifierPresetSlot{};
    }

    juce::var toVar() const
    {
        juce::Array<juce::var> arr;
        arr.ensureStorageAllocated(kNumPresets);
        for (int i = 0; i < kNumPresets; ++i)
            arr.add(slots[static_cast<size_t>(i)].toVar());
        return juce::var(arr);
    }

    static ModifierPresetBank fromVar(const juce::var& v)
    {
        ModifierPresetBank bank;
        if (! v.isArray()) return bank;

        auto* arr = v.getArray();
        const int n = juce::jmin((int) arr->size(), kNumPresets);
        for (int i = 0; i < n; ++i)
            bank.slots[static_cast<size_t>(i)] = ModifierPresetSlot::fromVar(arr->getReference(i));

        return bank;
    }
};
