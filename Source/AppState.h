/*
 ============================================================================== 
   AppState.h
   --------------------------------------------------------------------------
   Aggregates core singletons / managers for simplified passing to UI.
 ==============================================================================
*/

#pragma once

#include "ProjectManager.h"
#include "ModifierScheduler.h"
#include "AudioBufferManager.h"
#include "ChannelStrip.h"
#include "ModifierPreset.h"
#include "ModifierStickerOverlay.h"
#include "NodeClipDetector.h"

struct AppState : public ModifierSchedulerListener
{
    ProjectManager projectManager;
    AudioBufferManager bufferManager; // existing engine
    SessionSettings& settings = projectManager.getMutableSettings();
    ModifierScheduler scheduler { settings };

    // Platform-sized modifier state preset bank.
    ModifierPresetBank presetBank;

    // When >= 0, the next modifier trigger will apply this preset slot instead of the
    // randomly selected modifier.  Set by UI/MIDI recall, consumed by modifierTriggered().
    std::atomic<int> pendingPresetRecall { -1 };

    // Channel strips for FX placeholder wrapping existing buffers
    juce::OwnedArray<ChannelStrip> channelStrips;

    std::array<std::atomic<ModifierStickerOverlay::Mask>,
               AudioBufferManager::MAX_BUFFERS> activeModifierStickerMasks;

    // Per-node clip detection system (debug)
    ClipDetectorSystem clipDetector;

    AppState()
    {
        for (auto& mask : activeModifierStickerMasks)
            mask.store(0, std::memory_order_relaxed);

        // Initialize channel strips referencing underlying buffers
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            channelStrips.add(new ChannelStrip(bufferManager.getBuffer(i)));

        // Wire clip detector probe sets into each channel strip
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            channelStrips[i]->setClipProbes(&clipDetector.padProbes[(size_t)i]);

        // Wire clip detector into buffer manager for raw/pad/master probes
        bufferManager.setClipDetector(&clipDetector);

        // Hook per-buffer processor into AudioBufferManager to apply strip DSP (e.g., reverb)
        bufferManager.setPerBufferProcessor([this](int idx, juce::AudioBuffer<float>& temp, double sampleRate){
            if (!juce::isPositiveAndBelow(idx, channelStrips.size())) return;
            // Ensure DSP prepared (block size from temp buffer)
            channelStrips[idx]->prepareDSP(sampleRate, temp.getNumSamples());
            // Update envelopes were advanced per block already; process with current params
            channelStrips[idx]->processDSP(temp);
        });

        scheduler.addListener(this);
    }
    // Parts API
    void setActivePart(int partIndex)
    {
        settings.parts.activePart = juce::jlimit(0, settings.parts.getNumParts() - 1, partIndex);
        scheduler.refreshPlannedPartDestinations();
        // Compute per-buffer loop windows based on each buffer's own duration and the number of parts.
        auto loaded = bufferManager.getLoadedBufferIndices();
        int numParts = settings.parts.getNumParts();
        numParts = juce::jmax(1, numParts);
        for (int idx : loaded)
        {
            if (auto* b = bufferManager.getBuffer(idx))
            {
                const int64_t dur = (int64_t) b->getDurationInSamples();
                if (dur <= 0) continue;
                // Use proportional boundaries to avoid truncation issues on the last segment.
                const int64_t startS = (int64_t) ((settings.parts.activePart    * dur) / numParts);
                const int64_t endS   = (int64_t) (((settings.parts.activePart + 1) * dur) / numParts);
                const int64_t startClamped = juce::jlimit<int64_t>(0, dur - 1, startS);
                const int64_t endClamped   = juce::jlimit<int64_t>(startClamped + 1, dur, endS);
                b->setLoopWindow(startClamped, endClamped);
                b->setPlayheadSamples(startClamped);
                // If slicing is active, immediately re-trigger a slice inside the new part
                // to avoid a lull while waiting to hit the next slice boundary.
                if (b->isInSlicingMode() || b->isInContinuousRandomMode())
                {
                    b->triggerRandomSlice();
                    if (!b->isPlaying()) b->play();
                }
            }
        }
    }
    // Returns the start/end time in seconds for the active part for a given buffer index.
    // If the buffer index is invalid or not loaded, returns {0, 0}.
    std::pair<double,double> getActivePartSpanSecondsForBuffer(int bufferIndex) const
    {
        auto* b = bufferManager.getBuffer(bufferIndex);
        if (!b) return {0.0, 0.0};
        const double durSeconds = b->getDurationInSeconds();
        const int numParts = juce::jmax(1, settings.parts.getNumParts());
        const int active = juce::jlimit(0, numParts - 1, settings.parts.activePart);
        const double startSec = (active    * durSeconds) / numParts;
        const double endSec   = ((active + 1) * durSeconds) / numParts;
        return { startSec, juce::jmin(endSec, durSeconds) };
    }

    // Returns the start/end in musical bars for the active part for a given buffer.
    // Uses buffer's sample rate and SessionSettings BPM with 4/4 assumption (4 beats per bar).
    std::pair<double,double> getActivePartSpanBarsForBuffer(int bufferIndex) const
    {
        auto* b = bufferManager.getBuffer(bufferIndex);
        if (!b) return {0.0, 0.0};
    const double sr = b->getFileSampleRate();
        const double durSamples = (double) b->getDurationInSamples();
        if (sr <= 0.0 || durSamples <= 0.0) return {0.0, 0.0};
        const double durSeconds = durSamples / sr;
        const int numParts = juce::jmax(1, settings.parts.getNumParts());
        const int active = juce::jlimit(0, numParts - 1, settings.parts.activePart);
        const double startSec = (active    * durSeconds) / numParts;
        const double endSec   = ((active + 1) * durSeconds) / numParts;
        const double bpm = juce::jmax(1.0, settings.bpm);
        const double beatsPerSecond = bpm / 60.0;
        const double barsPerSecond = beatsPerSecond / 4.0; // 4/4 time
        return { startSec * barsPerSecond, endSec * barsPerSecond };
    }
    int getActivePart() const { return settings.parts.activePart; }
    int getPartLengthBars() const { return settings.parts.partLengthBars; }
    int getPartStartBar(int partIndex) const { return settings.parts.getPartStartBar(partIndex); }

    /// Apply the active part's loop window to a single buffer (e.g. after a new load).
    /// Unlike setActivePart(), this does NOT touch other buffers' playheads.
    void applyPartToBuffer(int bufferIndex)
    {
        auto* b = bufferManager.getBuffer(bufferIndex);
        if (b == nullptr) return;
        const int numParts = juce::jmax(1, settings.parts.getNumParts());
        const int64_t dur = (int64_t) b->getDurationInSamples();
        if (dur <= 0) return;
        const int64_t startS = (int64_t) ((settings.parts.activePart       * dur) / numParts);
        const int64_t endS   = (int64_t) (((settings.parts.activePart + 1) * dur) / numParts);
        const int64_t startClamped = juce::jlimit<int64_t>(0, dur - 1, startS);
        const int64_t endClamped   = juce::jlimit<int64_t>(startClamped + 1, dur, endS);
        b->setLoopWindow(startClamped, endClamped);
        b->setPlayheadSamples(startClamped);
    }

    /** Returns the modifiers that are currently in force on one pad.

        The sticker UI intentionally derives this from live engine state rather
        than trigger history.  That makes neutral toggles disappear, temporary
        effects expire with their envelopes, presets restore accurately, Reset
        clear immediately, and Swap Stack follow the state it actually moves.
    */
    ModifierStickerOverlay::Mask getActiveModifierStickerMask(int bufferIndex) const
    {
        if (! juce::isPositiveAndBelow(bufferIndex,
                                       static_cast<int>(activeModifierStickerMasks.size())))
            return 0;

        return activeModifierStickerMasks[static_cast<size_t>(bufferIndex)]
            .load(std::memory_order_acquire);
    }

    void refreshActiveModifierStickerMask(int bufferIndex)
    {
        using Stickers = ModifierStickerOverlay;
        Stickers::Mask mask = 0;

        if (! juce::isPositiveAndBelow(bufferIndex,
                                       static_cast<int>(activeModifierStickerMasks.size())))
            return;

        const auto* buffer = bufferManager.getBuffer(bufferIndex);
        if (buffer == nullptr || ! buffer->hasAudioLoaded()
            || ! juce::isPositiveAndBelow(bufferIndex, channelStrips.size()))
        {
            activeModifierStickerMasks[static_cast<size_t>(bufferIndex)]
                .store(0, std::memory_order_release);
            return;
        }

        const auto add = [&mask] (ModifierType type)
        {
            mask |= Stickers::bitForType(type);
        };

        constexpr double epsilon = 1.0e-6;
        const double speed = buffer->getSpeed();
        if (speed < -epsilon)
            add(ModifierType::Reverse);
        if (std::abs(std::abs(speed) - 1.0) > epsilon)
            add(ModifierType::Speed);
        if (std::abs(buffer->getStretchRatio() - 1.0) > epsilon)
            add(ModifierType::Stretch);

        const double pitch = buffer->getPitchSemiTones();
        if (pitch > epsilon)
            add(ModifierType::PitchUpOctave);
        else if (pitch < -epsilon)
            add(ModifierType::PitchDownOctave);

        if (buffer->isInSliceRepeaterMode())
            add(ModifierType::SliceRepeater);
        else if (buffer->isInArpSliceMode())
            add(ModifierType::ArpSlice);
        else if (buffer->isInContinuousRandomMode())
            add(ModifierType::BeatSliceRandom);

        if (buffer->isPingPongModeEnabled())
            add(ModifierType::PingPong);

        const auto* strip = channelStrips[bufferIndex];
        if (strip == nullptr)
        {
            activeModifierStickerMasks[static_cast<size_t>(bufferIndex)]
                .store(mask, std::memory_order_release);
            return;
        }

        const auto& fx = strip->effects();
        if (fx.delayEnabled)
        {
            if (! strip->isDubDelayBurstActive()
                || strip->hasPersistentDelayUnderDubBurst())
                add(ModifierType::BufferDelayOn);
            if (strip->isDubDelayBurstActive())
                add(ModifierType::BufferDelayDubBurst);
        }
        if (fx.reverbEnabled)
            add(ModifierType::BufferReverbOn);
        if (fx.lowPassEnabled)
            add(ModifierType::BufferLowPassOn);
        if (fx.highPassEnabled)
            add(ModifierType::BufferHighPassOn);
        if (fx.volumeRampEnabled)
            add(ModifierType::BufferShhhhhh);
        if (fx.tremoloEnabled)
            add(ModifierType::BufferTremolo);
        if (fx.chorusEnabled)
            add(ModifierType::BufferChorusOn);
        if (fx.autoPanEnabled)
            add(ModifierType::BufferAutoPan);
        if (fx.shLowPassEnabled)
            add(ModifierType::BufferSHLowPassOn);
        if (fx.shHighPassEnabled)
            add(ModifierType::BufferSHHighPassOn);
        if (fx.granularEnabled)
        {
            if (! strip->isTemporaryGranularActive()
                || strip->hasPersistentGranularUnderBurst())
                add(ModifierType::BufferGranularOn);
            if (strip->isTemporaryGranularActive())
                add(ModifierType::BufferGranularMomentary);
        }

        // Ducking is an always-on wet-path behavior, not a scheduled modifier.
        // Its glyph remains available in the renderer if the modifier returns.
        activeModifierStickerMasks[static_cast<size_t>(bufferIndex)]
            .store(mask, std::memory_order_release);
    }

    void refreshAllModifierStickerMasks()
    {
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            refreshActiveModifierStickerMask(i);
    }


    // ── Modifier presets ──────────────────────────────────────────────
    void capturePreset(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= ModifierPresetBank::kNumPresets) return;
        auto& slot = presetBank.slots[static_cast<size_t>(slotIndex)];
        slot.occupied = true;
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        {
            auto& snap = slot.buffers[static_cast<size_t>(i)];
            auto* buf = bufferManager.getBuffer(i);
            auto* strip = channelStrips[i];
            if (buf == nullptr || strip == nullptr) continue;

            // AudioBuffer transform state
            snap.speed                   = buf->getSpeed();
            snap.stretchRatio            = buf->getStretchRatio();
            snap.pitchSemiTones          = buf->getPitchSemiTones();
            snap.continuousRandomSlicing = buf->isInContinuousRandomMode();
            snap.numSlices               = buf->getNumSlices();
            snap.arpSliceActive          = buf->isInArpSliceMode() && !buf->isInSliceRepeaterMode();
            snap.arpSliceRepeaterMode    = buf->isInSliceRepeaterMode();
            snap.arpSequenceLength       = buf->getArpSequenceLength();
            snap.arpRepeatBars           = buf->getArpRepeatBars();
            snap.arpTotalSlices          = buf->getArpTotalSlices();
            snap.pingPongEnabled         = buf->isPingPongModeEnabled();
            snap.pingPongDivision        = buf->getPingPongDivision();

            // ChannelStrip FX enable flags
            const auto& fx = strip->effects();
            snap.delayEnabled      = fx.delayEnabled;
            snap.reverbEnabled     = fx.reverbEnabled;
            snap.lowPassEnabled    = fx.lowPassEnabled;
            snap.highPassEnabled   = fx.highPassEnabled;
            snap.tremoloEnabled    = fx.tremoloEnabled;
            snap.chorusEnabled     = fx.chorusEnabled;
            snap.autoPanEnabled    = fx.autoPanEnabled;
            snap.volumeRampEnabled = fx.volumeRampEnabled;
            snap.shLowPassEnabled  = fx.shLowPassEnabled;
            snap.shHighPassEnabled = fx.shHighPassEnabled;
            snap.granularEnabled   = fx.granularEnabled;

            // ChannelStrip FxParams
            const auto& fp = strip->getFxParams();
            snap.reverbWet              = fp.reverbWet;
            snap.reverbPreDelayMs       = fp.reverbPreDelayMs;
            snap.delayFeedback          = fp.delayFeedback;
            snap.delayTimeMs            = fp.delayTimeMs;
            snap.delayWet               = fp.delayWet;
            snap.delayPingPong          = fp.delayPingPong;
            snap.delayFeedbackHighCutHz = fp.delayFeedbackHighCutHz;
            snap.delayFbDrive           = fp.delayFbDrive;
            snap.duckingEnabled         = fp.duckingEnabled;
            snap.duckAmount             = fp.duckAmount;
            snap.duckReleaseMs          = fp.duckReleaseMs;
            snap.wowFlutterEnabled      = fp.wowFlutterEnabled;
            snap.wowDepthMs             = fp.wowDepthMs;
            snap.wowRateHz              = fp.wowRateHz;
            snap.flutterDepthMs         = fp.flutterDepthMs;
            snap.flutterRateHz          = fp.flutterRateHz;
            snap.wowPeriodBars          = fp.wowPeriodBars;
            snap.flutterPeriodBars      = fp.flutterPeriodBars;
            snap.lowPassCutoff          = fp.lowPassCutoff;
            snap.highPassCutoff         = fp.highPassCutoff;
            snap.tremoloDepth           = fp.tremoloDepth;
            snap.tremoloRateHz          = fp.tremoloRateHz;
            snap.chorusDepth            = fp.chorusDepth;
            snap.chorusRateHz           = fp.chorusRateHz;
            snap.chorusMix              = fp.chorusMix;
            snap.chorusDelayMs          = fp.chorusDelayMs;
            snap.panRateHz              = fp.panRateHz;
            snap.panDepth               = fp.panDepth;
            snap.panMix                 = fp.panMix;
            snap.panPeriodBars          = fp.panPeriodBars;
            snap.volumeGain             = fp.volumeGain;
            snap.shLpfCutoff            = fp.shLpfCutoff;
            snap.shLpfQ                 = fp.shLpfQ;
            snap.shHpfCutoff            = fp.shHpfCutoff;
            snap.shHpfQ                 = fp.shHpfQ;
            snap.shDivisionBars         = fp.shDivisionBars;
            snap.grainDensityHz         = fp.grainDensityHz;
            snap.grainSizeMs            = fp.grainSizeMs;
            snap.grainPitchSpread       = fp.grainPitchSpread;
            snap.grainMix               = fp.grainMix;
            snap.grainTexture           = fp.grainTexture;
        }
    }

    void restorePreset(int slotIndex)
    {
        if (slotIndex < 0 || slotIndex >= ModifierPresetBank::kNumPresets) return;
        const auto& slot = presetBank.slots[static_cast<size_t>(slotIndex)];
        if (! slot.occupied) return;
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
        {
            const auto& snap = slot.buffers[static_cast<size_t>(i)];
            auto* buf = bufferManager.getBuffer(i);
            auto* strip = channelStrips[i];
            if (buf == nullptr || strip == nullptr) continue;

            // AudioBuffer transform state
            buf->setSpeed(snap.speed);
            buf->setStretchRatio(snap.stretchRatio);
            buf->setPitchSemiTones(snap.pitchSemiTones);
            if (snap.arpSliceRepeaterMode)
            {
                buf->startSliceRepeater(snap.arpTotalSlices, snap.arpSequenceLength);
            }
            else if (snap.arpSliceActive)
            {
                buf->startArpSlicing(snap.arpTotalSlices, snap.arpSequenceLength, snap.arpRepeatBars);
            }
            else if (snap.continuousRandomSlicing)
            {
                buf->setNumSlices(snap.numSlices);
                buf->startContinuousRandomSlicing();
            }
            else
            {
                // Fully exit slicing mode so the buffer doesn't get stuck in a
                // single-slice boundary check that stops playback.
                buf->exitSlicingMode();
            }
            buf->setPingPongMode(snap.pingPongEnabled, snap.pingPongDivision,
                                 settings.bpm, buf->getFileSampleRate());

            // Presets describe a settled snapshot, never an in-flight fade.
            // Cancel old controllers before assigning the captured state so a
            // pre-recall burst cannot later overwrite the restored stack.
            strip->clearModifierAutomation();

            // ChannelStrip: apply the captured FX state.
            auto& fx = strip->effects();
            fx.delayEnabled      = snap.delayEnabled;
            fx.reverbEnabled     = snap.reverbEnabled;
            fx.lowPassEnabled    = snap.lowPassEnabled;
            fx.highPassEnabled   = snap.highPassEnabled;
            fx.tremoloEnabled    = snap.tremoloEnabled;
            fx.chorusEnabled     = snap.chorusEnabled;
            fx.autoPanEnabled    = snap.autoPanEnabled;
            // Shhhhhh is momentary. A preset may have been captured while
            // it was passing through, but recall must not create a permanent
            // sticker with no controller available to finish it.
            fx.volumeRampEnabled = false;
            fx.shLowPassEnabled  = snap.shLowPassEnabled;
            fx.shHighPassEnabled = snap.shHighPassEnabled;
            fx.granularEnabled   = snap.granularEnabled;

            auto& fp = strip->getMutableFxParams();
            fp.reverbWet              = snap.reverbWet;
            fp.reverbPreDelayMs       = snap.reverbPreDelayMs;
            fp.delayFeedback          = snap.delayFeedback;
            fp.delayTimeMs            = snap.delayTimeMs;
            fp.delayWet               = snap.delayWet;
            fp.delayPingPong          = snap.delayPingPong;
            fp.delayFeedbackHighCutHz = snap.delayFeedbackHighCutHz;
            fp.delayFbDrive           = snap.delayFbDrive;
            fp.duckingEnabled         = snap.duckingEnabled;
            fp.duckAmount             = snap.duckAmount;
            fp.duckReleaseMs          = snap.duckReleaseMs;
            fp.wowFlutterEnabled      = snap.wowFlutterEnabled;
            fp.wowDepthMs             = snap.wowDepthMs;
            fp.wowRateHz              = snap.wowRateHz;
            fp.flutterDepthMs         = snap.flutterDepthMs;
            fp.flutterRateHz          = snap.flutterRateHz;
            fp.wowPeriodBars          = snap.wowPeriodBars;
            fp.flutterPeriodBars      = snap.flutterPeriodBars;
            fp.lowPassCutoff          = snap.lowPassCutoff;
            fp.highPassCutoff         = snap.highPassCutoff;
            fp.tremoloDepth           = snap.tremoloDepth;
            fp.tremoloRateHz          = snap.tremoloRateHz;
            fp.chorusDepth            = snap.chorusDepth;
            fp.chorusRateHz           = snap.chorusRateHz;
            fp.chorusMix              = snap.chorusMix;
            fp.chorusDelayMs          = snap.chorusDelayMs;
            fp.panRateHz              = snap.panRateHz;
            fp.panDepth               = snap.panDepth;
            fp.panMix                 = snap.panMix;
            fp.panPeriodBars          = snap.panPeriodBars;
            fp.volumeGain             = snap.volumeRampEnabled ? 1.0f
                                                                : snap.volumeGain;
            fp.shLpfCutoff            = snap.shLpfCutoff;
            fp.shLpfQ                 = snap.shLpfQ;
            fp.shHpfCutoff            = snap.shHpfCutoff;
            fp.shHpfQ                 = snap.shHpfQ;
            fp.shDivisionBars         = snap.shDivisionBars;
            fp.grainDensityHz         = snap.grainDensityHz;
            fp.grainSizeMs            = snap.grainSizeMs;
            fp.grainPitchSpread       = snap.grainPitchSpread;
            fp.grainMix               = snap.grainMix;
            fp.grainTexture           = snap.grainTexture;

            // Reset playhead to the beginning of the file (or loop bracket start
            // if parts are active) so preset recall behaves like a fresh start,
            // matching the state at capture time (e.g. after a ResetAll modifier).
            if (buf->isLoopWindowEnabled())
                buf->setPlayheadSamples(buf->getLoopStartSamples());
            else
                buf->setPlayheadSamples(0);

            // Ensure playback continues if the buffer has audio loaded
            if (buf->hasAudioLoaded() && !buf->isPlaying())
                buf->play();
        }

        refreshAllModifierStickerMasks();
    }

    ~AppState() override
    {
        scheduler.removeListener(this);
    }

    // ModifierSchedulerListener
    void upcomingModifierChanged(const ModifierDescriptor& desc) override
    {
        juce::ignoreUnused(desc);
        // Future: update UI labels
    }

    void musicalCueReached() override
    {
        bufferManager.startBuffersAwaitingMusicalCue();
    }

    void modifierTriggered(const ModifierDescriptor& desc, const juce::Array<int>& targets) override
    {
        // §4.2  A modifier trigger is a musically relevant cue — start any
        // buffers that were loaded mid-transport and deferred until now.
        bufferManager.startBuffersAwaitingMusicalCue();

        // If a preset recall is pending, apply the preset instead of the
        // scheduled modifier.  The preset replaces the modifier entirely.
        const int presetSlot = pendingPresetRecall.exchange(-1);
        if (presetSlot >= 0 && presetSlot < ModifierPresetBank::kNumPresets
            && presetBank.isSlotOccupied(presetSlot))
        {
            restorePreset(presetSlot);
            return; // skip normal modifier application
        }

        switch (desc.type)
        {
            case ModifierType::Reverse:
                applyReverse(targets);
                break;
            case ModifierType::Speed:
                applySpeed(desc, targets);
                break;
            case ModifierType::Stretch:
                applyStretch(desc, targets);
                break;
            case ModifierType::PitchUpOctave:
                applyPitchSemiTones(+12.0, targets);
                break;
            case ModifierType::PitchDownOctave:
                applyPitchSemiTones(-12.0, targets);
                break;
            case ModifierType::ResetAll:
                applyReset(targets);
                break;
            case ModifierType::BeatSliceRandom:
                applyBeatSliceRandom(desc, targets);
                break;
            case ModifierType::ArpSlice:
                applyArpSlice(desc, targets);
                break;
            case ModifierType::SliceRepeater:
                applySliceRepeater(desc, targets);
                break;
            case ModifierType::PingPong:
                applyPingPong(desc, targets);
                break;
            case ModifierType::BufferReverbOn:
                applyBufferReverbOn(desc, targets);
                break;
            case ModifierType::BufferDelayOn:
                applyBufferDelayOn(desc, targets);
                break;
            case ModifierType::BufferDelayDubBurst:
                applyBufferDelayDubBurst(desc, targets);
                break;
            case ModifierType::BufferLowPassOn:
                applyBufferLowPassOn(targets);
                break;
            case ModifierType::BufferHighPassOn:
                applyBufferHighPassOn(targets);
                break;
            case ModifierType::BufferSHLowPassOn:
                applyBufferSHLowPassOn(desc, targets);
                break;
            case ModifierType::BufferSHHighPassOn:
                applyBufferSHHighPassOn(desc, targets);
                break;
            case ModifierType::MasterLowPassOn:
            {
                // Temporary global LPF applied per track: ramp up then down, or jump then decay
                double bars = desc.plannedFxFadeBars.value_or(4.0);
                bool jump = desc.plannedImmediateJump.value_or(false);
                const float targetCut = 1200.0f; // low-pass to ~1.2kHz
                for (int i = 0; i < channelStrips.size(); ++i)
                {
                    auto& strip = *channelStrips[i];
                    strip.effects().lowPassEnabled = true;
                    float startCut = strip.getFxParams().lowPassCutoff;
                    if (jump)
                    {
                        // Jump to target immediately, then ramp back to start over full duration
                        strip.getMutableFxParams().lowPassCutoff = targetCut;
                        strip.setLowPassCutoffEnvelope(targetCut, startCut, (float)bars);
                    }
                    else
                    {
                        float half = (float)juce::jmax(0.0001, bars * 0.5);
                        strip.startTemporaryLowPass(targetCut, half, startCut, half);
                    }
                }
                break;
            }
            case ModifierType::MasterHighPassOn:
            {
                // Temporary global HPF applied per track
                double bars = desc.plannedFxFadeBars.value_or(4.0);
                bool jump = desc.plannedImmediateJump.value_or(false);
                const float targetCut = 180.0f; // high-pass to ~180Hz
                for (int i = 0; i < channelStrips.size(); ++i)
                {
                    auto& strip = *channelStrips[i];
                    strip.effects().highPassEnabled = true;
                    float startCut = strip.getFxParams().highPassCutoff;
                    if (jump)
                    {
                        strip.getMutableFxParams().highPassCutoff = targetCut;
                        strip.setHighPassCutoffEnvelope(targetCut, startCut, (float)bars);
                    }
                    else
                    {
                        float half = (float)juce::jmax(0.0001, bars * 0.5);
                        strip.startTemporaryHighPass(targetCut, half, startCut, half);
                    }
                }
                break;
            }
            case ModifierType::BufferTremolo:
                applyBufferTremoloOn(targets);
                break;
            case ModifierType::BufferShhhhhh:
                applyBufferShhhhhh(desc, targets);
                break;
            case ModifierType::BufferChorusOn:
                applyBufferChorusOn(desc, targets);
                break;
            case ModifierType::BufferAutoPan:
                applyBufferAutoPan(desc, targets);
                break;
            case ModifierType::BufferGranularOn:
                applyBufferGranularOn(desc, targets);
                break;
            case ModifierType::BufferGranularMomentary:
                applyBufferGranularMomentary(desc, targets);
                break;
            case ModifierType::SwitchPart:
            {
                int num = settings.parts.getNumParts();
                if (num >= 1)
                {
                    int current = settings.parts.activePart;
                    const int target = desc.plannedDestinationPart.has_value()
                                           ? juce::jlimit (0, num - 1, *desc.plannedDestinationPart)
                                           : (num == 1 ? 0 : (current + 1) % num);
                    setActivePart(target);
                }
                break;
            }
            // Ducking is enabled by default; no explicit modifier trigger.
            case ModifierType::SwapModifierStack:
                applySwapModifierStack(targets);
                break;
            case ModifierType::BufferDuckingOn:
            case ModifierType::QuarterNoteBurst:
            case ModifierType::Unknown:
                break;
        }

        refreshAllModifierStickerMasks();
    }
private:
    void applyPitchSemiTones(double deltaSemiTones, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                // Pitch shift now coexists with reverse and slicing; source-level
                // crossfades keep SoundTouch's input smooth so no flush is needed.
                const double current = b->getPitchSemiTones();
                b->setPitchSemiTones(current + deltaSemiTones);
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applyReverse(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                // T6: Toggle direction — flip the sign rather than always going negative.
                // Reverse now coexists with SoundTouch stretch/pitch; the source-level
                // crossfade and SoundTouch's overlap-add handle transitions smoothly.
                double s = b->getSpeed();
                if (juce::approximatelyEqual (s, 0.0))
                    s = 1.0;
                b->setSpeed(-s);  // flip sign: forward→reverse, reverse→forward
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applySpeed(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        // Use structured planned speed if provided; else default to 1.0 (no change)
        double speedVal = desc.plannedSpeed.has_value() ? desc.plannedSpeed.value() : 1.0;
        speedVal = juce::approximatelyEqual (speedVal, 0.0) ? 1.0 : std::abs (speedVal);
        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                // Preserve current direction: negative speed means reverse.
                double current = b->getSpeed();
                if (juce::approximatelyEqual (current, 0.0))
                    current = 1.0;
                const double signedSpeed = (current < 0.0 ? -speedVal : speedVal);
                b->setSpeed(signedSpeed);
                // Speed and stretch now coexist; SoundTouch routes speed magnitude
                // through its rate/tempo controls alongside the stretch ratio.
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applyStretch(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        const double ratioVal = desc.plannedStretch.has_value() ? desc.plannedStretch.value() : 1.0;
        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                // Stretch now coexists with reverse and slicing; source-level crossfades
                // keep SoundTouch's input smooth so no flush is needed.
                b->setStretchRatio(ratioVal);
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applyReset(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        auto playing = bufferManager.getPlayingBufferIndices();
        for (int idx : targets)
        {
            bool wasPlaying = playing.contains(idx);
            // Reset audio buffer + FX strip state
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                // ChannelStrip::reset also resets underlying buffer to defaults
                channelStrips[idx]->reset();
            }
            else if (auto* b = bufferManager.getBuffer(idx))
            {
                b->resetToDefaults();
            }
            if (auto* b = bufferManager.getBuffer(idx))
            {
                b->resetToBeginning();
                if (wasPlaying && b->hasAudioLoaded()) b->play();
            }
        }
    }

    void applyBeatSliceRandom(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        // If plannedSliceDivision contains a numeric value, interpret it as direct slice count.
        int plannedSlices = 0;
        if (desc.plannedSliceDivision.isNotEmpty())
        {
            // Accept pure numeric strings like "16"; ignore non-numeric labels.
            if (desc.plannedSliceDivision.containsOnly("0123456789"))
                plannedSlices = desc.plannedSliceDivision.getIntValue();
        }
        double secondsPerBar = settings.getSecondsPerBar();
        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                // Slicing now coexists with SoundTouch stretch and pitch; source-level
                // crossfades keep SoundTouch's input smooth so no flush is needed.

                int slices = plannedSlices;
                if (slices <= 0)
                {
                    // Fallback: compute a reasonable slice count based on duration and 1/8 division
                    double durSeconds = b->getDurationInSeconds();
                    if (durSeconds <= 0.0) continue;
                    double approxBars = durSeconds / secondsPerBar;
                    double barsFactor = juce::jmax(1.0, approxBars);
                    double slicesD = 2.0 * settings.getBeatsPerBar() * barsFactor; // 1/8 default
                    slices = juce::jlimit(4, 64, (int)std::round(slicesD));
                }
                // Clamp to supported counts
                slices = juce::jlimit(4, 64, slices);
                if (slices < 2) continue;
                b->setNumSlices(slices);
                b->startContinuousRandomSlicing();
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applyArpSlice(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        // Determine parameters from planned fields or defaults
        int seqLen = desc.plannedArpSequenceLength.value_or(4);
        int totalSlices = desc.plannedArpTotalSlices.value_or(16);
        int repeatBars = desc.plannedArpRepeatBars.value_or(2);

        seqLen = juce::jlimit(1, 8, seqLen);
        totalSlices = juce::jlimit(16, 64, totalSlices);
        repeatBars = juce::jmax(1, repeatBars);

        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                b->startArpSlicing(totalSlices, seqLen, repeatBars);
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applySliceRepeater(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        int reps = desc.plannedSliceRepeaterReps.value_or(8);
        int totalSlices = desc.plannedSliceRepeaterTotal.value_or(16);

        reps = juce::jlimit(4, 32, reps);
        totalSlices = juce::jlimit(16, 64, totalSlices);

        const double secondsPerBar = settings.getSecondsPerBar();

        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                // Cap repetitions so one slice never repeats for more than 1 bar
                double bufDur = b->getDurationInSeconds();
                if (bufDur > 0.0 && secondsPerBar > 0.0)
                {
                    double sliceDur = bufDur / (double)totalSlices;
                    int maxReps = (int)std::floor(secondsPerBar / sliceDur);
                    maxReps = juce::jmax(1, maxReps);
                    reps = juce::jmin(reps, maxReps);
                }

                b->startSliceRepeater(totalSlices, reps);
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applyPingPong(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        // Scheduled descriptors always carry this value. Keep a deterministic
        // quarter-note fallback for manually constructed legacy descriptors.
        const double division = desc.plannedPingPongDivision.value_or (0.25);
        
        const double bpm = settings.bpm;
        
        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                const double sampleRate = b->getFileSampleRate();
                b->setPingPongMode(true, division, bpm, sampleRate);
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applyBufferReverbOn(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().reverbEnabled = true;
                // Use planned wet value if provided; else default to 0.75
                float targetWet = desc.plannedWet.has_value() ? (float)desc.plannedWet.value() : 0.75f;
                // Planned fade duration (bars): 0 = instant, else ramp over bars
                float durationBars = desc.plannedFxFadeBars.has_value() ? (float)desc.plannedFxFadeBars.value() : 1.0f;
                strip.setReverbWetEnvelope(strip.getFxParams().reverbWet, targetWet, durationBars);
                // Standardize pre-delay: fixed 20 ms for all reverb states to avoid jumping
                strip.setReverbPreDelayEnvelope(strip.getFxParams().reverbPreDelayMs, 20.0f, 0.5f);
            }
        }
    }

    void applyBufferReverbOff(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                // Ramp wet down to 0 over 2 bars; then disable flag
                strip.setReverbWetEnvelope(strip.getFxParams().reverbWet, 0.0f, 2.0f);
                strip.effects().reverbEnabled = true; // keep on during ramp
            }
        }
    }

    void applyBufferDelayOn(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.cancelDubDelayBurst();
                strip.effects().delayEnabled = true;
                // Division handling kept as before
                // Wet/feedback targets
                double targetWet = desc.plannedDelayWet.value_or(strip.getFxParams().delayWet);
                double targetFb  = desc.plannedDelayFeedback.value_or(strip.getFxParams().delayFeedback);
                double fadeBars  = desc.plannedFxFadeBars.value_or(0.0);
                if (fadeBars <= 0.0)
                {
                    strip.getMutableFxParams().delayWet = (float) juce::jlimit(0.0, 1.0, targetWet);
                    strip.getMutableFxParams().delayFeedback = (float) juce::jlimit(0.0, 0.95, targetFb);
                }
                else
                {
                    // Ramp both wet and feedback over fadeBars
                    strip.setDelayWetEnvelope(strip.getFxParams().delayWet, (float) juce::jlimit(0.0, 1.0, targetWet), (float) fadeBars);
                    strip.setDelayFeedbackEnvelope(strip.getFxParams().delayFeedback, (float) juce::jlimit(0.0, 0.95, targetFb), (float) fadeBars);
                }
                // Optional flags from planned descriptor
                if (desc.plannedDelayPingPong.has_value())
                    strip.getMutableFxParams().delayPingPong = desc.plannedDelayPingPong.value();
                if (desc.plannedWowFlutter.has_value())
                {
                    strip.getMutableFxParams().wowFlutterEnabled = desc.plannedWowFlutter.value();
                    // If enabling wow/flutter, keep existing default depths/rates; tempo-sync handled elsewhere
                }
                // Ensure enabled
                strip.effects().delayEnabled = true;
            }
        }
    }

    void applyBufferDelayOff(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                // Ramp feedback down to zero over 2 bars; then auto-disable will clear flag
                strip.setDelayFeedbackEnvelope(strip.getFxParams().delayFeedback, 0.0f, 2.0f);
                // Optionally reduce wet immediately for a cleaner tail decay
                strip.getMutableFxParams().delayWet = 0.20f;
                strip.effects().delayEnabled = true; // keep on during ramp
            }
        }
    }

    void applyBufferDelayPingPong(const juce::Array<int>& targets, bool enabled)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().delayEnabled = true; // ensure on
                strip.getMutableFxParams().delayPingPong = enabled;
            }
        }
    }

    void applyBufferDelayWowFlutter(const juce::Array<int>& targets, bool enabled)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().delayEnabled = true; // ensure delay active
                strip.getMutableFxParams().wowFlutterEnabled = enabled;
                if (enabled)
                {
                    // Randomize subtle, tempo-synced movement
                    juce::Random rng;
                    // Wow period choices in bars: 1/4 note (0.25), 1/2 (0.5), 1 (whole note), 2 bars, 4 bars
                    const float wowChoices[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                    const int wIdx = rng.nextInt({0, (int) (sizeof(wowChoices)/sizeof(wowChoices[0]))});
                    const float wowPeriodBars = wowChoices[wIdx];
                    // Flutter period choices in bars: 1 beat (0.25), 1/2 bar (0.5), 1 bar (1.0), 2 bars (2.0)
                    const float flutterChoices[] = { 0.25f, 0.5f, 1.0f, 2.0f };
                    const int fIdx = rng.nextInt({0, (int) (sizeof(flutterChoices)/sizeof(flutterChoices[0]))});
                    const float flutterPeriodBars = flutterChoices[fIdx];
                    // Depth choices: light to medium
                    const float wowDepthOpts[] = { 0.8f, 1.2f, 1.5f, 2.0f };
                    const float flutterDepthOpts[] = { 0.2f, 0.3f, 0.5f };
                    strip.getMutableFxParams().wowDepthMs = wowDepthOpts[rng.nextInt({0, (int)(sizeof(wowDepthOpts)/sizeof(wowDepthOpts[0]))})];
                    strip.getMutableFxParams().flutterDepthMs = flutterDepthOpts[rng.nextInt({0, (int)(sizeof(flutterDepthOpts)/sizeof(flutterDepthOpts[0]))})];
                    // Compute rates from periods
                    const double spbar = settings.getSecondsPerBar();
                    const double wowHz = spbar > 0.0 ? (1.0 / (spbar * wowPeriodBars)) : 0.25;
                    const double flutterHz = spbar > 0.0 ? (1.0 / (spbar * flutterPeriodBars)) : 0.5;
                    strip.getMutableFxParams().wowRateHz = (float) wowHz;
                    strip.getMutableFxParams().flutterRateHz = (float) flutterHz;
                    // Store periods to preserve behavior across BPM changes
                    strip.getMutableFxParams().wowPeriodBars = wowPeriodBars;
                    strip.getMutableFxParams().flutterPeriodBars = flutterPeriodBars;
                }
            }
        }
    }

    void applyBufferDelayDubBurst(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        // Scale the delay wet level down as more pads are targeted so the
        // summed delay output doesn't compound into excessive volume.
        // Inverse-square-root scaling keeps multi-pad delay bursts controlled.
        const float padScaling = 1.0f / std::sqrt((float) targets.size());
        // Dub burst: ramp feedback up quickly, then down to zero, auto-disable after tail
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.prepareDubDelayBurst();
                strip.effects().delayEnabled = true;
                // Determine tap times similar to Delay On
                double beatMs = settings.getSecondsPerBeat() * 1000.0;
                juce::Array<float> tapTimesMs;
                auto mapDivisionToMult = [](const juce::String& label)->double{
                    if (label == "1/4") return 1.0;
                    if (label == "1/8") return 0.5;
                    if (label == "1/8D") return 0.75;
                    if (label == "1/8T") return 1.0/3.0;
                    return 1.0;
                };
                if (!desc.plannedDelayDivisions.isEmpty())
                {
                    for (auto d : desc.plannedDelayDivisions)
                    {
                        double mult = mapDivisionToMult(d);
                        tapTimesMs.add((float) juce::jlimit(1.0, 2000.0, beatMs * mult));
                    }
                }
                else if (desc.plannedDelayDivision.isNotEmpty())
                {
                    double mult = mapDivisionToMult(desc.plannedDelayDivision);
                    tapTimesMs.add((float) juce::jlimit(1.0, 2000.0, beatMs * mult));
                }
                else
                {
                    // Default to dotted 1/8-note burst if unspecified
                    tapTimesMs.add((float) juce::jlimit(1.0, 2000.0, beatMs * 0.75));
                }
                strip.setDelayTapTimesMs(tapTimesMs);
                // Push it into dubby territory
                // - Higher wet
                // - Darker repeats via lower high-cut
                // - Ping-pong on for width
                // - Add drive to feedback loop for saturation
                strip.getMutableFxParams().delayWet = (float) juce::jlimit(0.0, 1.0, desc.plannedDelayWet.value_or(0.80) * padScaling);
                strip.getMutableFxParams().delayFeedbackHighCutHz = 3500.0f;
                strip.getMutableFxParams().delayPingPong = true;
                strip.getMutableFxParams().delayFbDrive = 1.8f;
                // Start rising feedback envelope (faster rise, higher target)
                float startFb = strip.getFxParams().delayFeedback;
                float riseTarget = (float) juce::jlimit(0.0, 0.95, desc.plannedDelayFeedback.value_or(0.88));
                strip.setDelayFeedbackEnvelope(startFb, riseTarget, 0.5f);
                // Mark dub burst fall parameters inside strip (longer decay)
                strip.startDubDelayBurst(riseTarget, 0.5f, 0.0f, 4.0f);
            }
        }
    }

    void applyBufferLowPassOn(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().lowPassEnabled = true;
                // Sweep LPF cutoff down to 4000 Hz over 1 bar
                strip.setLowPassCutoffEnvelope(strip.getFxParams().lowPassCutoff, 4000.0f, 1.0f);
            }
        }
    }

    void applyBufferLowPassOff(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                // Reset cutoff back to default (20k) over 1 bar
                strip.setLowPassCutoffEnvelope(strip.getFxParams().lowPassCutoff, 20000.0f, 1.0f);
                strip.effects().lowPassEnabled = true; // keep on during ramp
            }
        }
    }

    void applyBufferHighPassOn(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().highPassEnabled = true;
                // Raise HPF cutoff up to 120 Hz over 1 bar
                strip.setHighPassCutoffEnvelope(strip.getFxParams().highPassCutoff, 120.0f, 1.0f);
            }
        }
    }

    void applyBufferHighPassOff(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                // Reset HPF cutoff down to default (20 Hz) over 1 bar
                strip.setHighPassCutoffEnvelope(strip.getFxParams().highPassCutoff, 20.0f, 1.0f);
                strip.effects().highPassEnabled = true; // keep on during ramp
            }
        }
    }

    void applyBufferSHLowPassOn(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        float divBars = (float) desc.plannedSHDivisionBars.value_or(0.25);
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().shLowPassEnabled = true;
                strip.getMutableFxParams().shDivisionBars = divBars;
                // Immediately seed initial S&H values
                juce::Random rng;
                strip.getMutableFxParams().shLpfCutoff = 200.0f + rng.nextFloat() * (8000.0f - 200.0f);
                strip.getMutableFxParams().shLpfQ = 0.5f + rng.nextFloat() * (4.0f - 0.5f);
            }
        }
    }

    void applyBufferSHHighPassOn(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        float divBars = (float) desc.plannedSHDivisionBars.value_or(0.25);
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().shHighPassEnabled = true;
                strip.getMutableFxParams().shDivisionBars = divBars;
                // Immediately seed initial S&H values
                juce::Random rng;
                strip.getMutableFxParams().shHpfCutoff = 60.0f + rng.nextFloat() * (800.0f - 60.0f);
                strip.getMutableFxParams().shHpfQ = 0.5f + rng.nextFloat() * (4.0f - 0.5f);
            }
        }
    }

    void applyBufferShhhhhh(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;

        const float rampBars = (float) desc.plannedFxFadeBars.value_or (2.0);
        const float holdBars = (float) desc.plannedVolumeHoldBars.value_or (2.0);

        // Shhhhhh currently has one fixed destination: silence.
        const float targetGain = 0.0f; // Fade to silence

        // Ramp back up over the same number of bars as the ramp down
        const float rampUpBars = rampBars;

        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.startTemporaryVolumeRamp(targetGain, rampBars, holdBars, rampUpBars);
            }
        }
    }

    void applyBufferTremoloOn(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().tremoloEnabled = true;
                // Increase tremolo depth to 0.5 over 2 bars
                strip.setTremoloDepthEnvelope(strip.getFxParams().tremoloDepth, 0.5f, 2.0f);
                // Set rate to 1/8-note relative to BPM
                double secondsPerBeat = settings.getSecondsPerBeat();
                double rateHz = secondsPerBeat > 0.0 ? (2.0 / secondsPerBeat) : 4.0; // 1/8-note
                strip.getMutableFxParams().tremoloRateHz = (float) rateHz;
            }
        }
    }

    void applyBufferTremoloOff(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.setTremoloDepthEnvelope(strip.getFxParams().tremoloDepth, 0.0f, 1.5f);
                strip.effects().tremoloEnabled = true; // keep on during ramp
            }
        }
    }

    void applyBufferChorusOn(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().chorusEnabled = true;
                // Set chorus parameters from planned values or defaults
                float targetMix = desc.plannedChorusMix.has_value() ? (float)desc.plannedChorusMix.value() : 0.5f;
                float depth = desc.plannedChorusDepth.has_value() ? (float)desc.plannedChorusDepth.value() : 0.5f;
                float rateHz = desc.plannedChorusRateHz.has_value() ? (float)desc.plannedChorusRateHz.value() : 1.0f;
                float durationBars = desc.plannedFxFadeBars.has_value() ? (float)desc.plannedFxFadeBars.value() : 1.0f;
                strip.getMutableFxParams().chorusDepth = depth;
                strip.getMutableFxParams().chorusRateHz = rateHz;
                // Ramp mix from current to target over durationBars
                strip.setChorusMixEnvelope(strip.getFxParams().chorusMix, targetMix, durationBars);
            }
        }
    }

    void applyBufferChorusOff(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                // Ramp mix down to 0 over 2 bars; auto-disable will clear flag
                strip.setChorusMixEnvelope(strip.getFxParams().chorusMix, 0.0f, 2.0f);
                strip.effects().chorusEnabled = true; // keep on during ramp
            }
        }
    }

    void applyBufferAutoPan(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        const double spbar = settings.getSecondsPerBar();
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().autoPanEnabled = true;
                float targetMix = desc.plannedPanMix.has_value() ? (float)desc.plannedPanMix.value() : 0.5f;
                float depth = desc.plannedPanDepth.has_value() ? (float)desc.plannedPanDepth.value() : 1.0f;
                float rateHz = desc.plannedPanRateHz.has_value() ? (float)desc.plannedPanRateHz.value() : 2.0f;
                float durationBars = desc.plannedFxFadeBars.has_value() ? (float)desc.plannedFxFadeBars.value() : 1.0f;
                strip.getMutableFxParams().panDepth = depth;
                strip.getMutableFxParams().panRateHz = rateHz;
                // Store period in bars for BPM resync (derive from rate and current tempo)
                if (spbar > 0.0 && rateHz > 0.0f)
                    strip.getMutableFxParams().panPeriodBars = (float)(1.0 / (rateHz * spbar));
                // Ramp mix from current to target
                strip.setPanMixEnvelope(strip.getFxParams().panMix, targetMix, durationBars);
            }
        }
    }

    void applyBufferAutoPanOff(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.setPanMixEnvelope(strip.getFxParams().panMix, 0.0f, 2.0f);
                strip.effects().autoPanEnabled = true; // keep on during ramp
            }
        }
    }

    void applyBufferGranularOn(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.cancelTemporaryGranular();
                strip.effects().granularEnabled = true;
                float targetMix   = desc.plannedGrainMix.has_value()         ? (float)desc.plannedGrainMix.value()         : 0.75f;
                float density     = desc.plannedGrainDensityHz.has_value()   ? (float)desc.plannedGrainDensityHz.value()   : 8.0f;
                float size        = desc.plannedGrainSizeMs.has_value()      ? (float)desc.plannedGrainSizeMs.value()      : 150.0f;
                float pitchSpread = desc.plannedGrainPitchSpread.has_value() ? (float)desc.plannedGrainPitchSpread.value() : 0.0f;
                float texture     = desc.plannedGrainTexture.has_value()     ? (float)desc.plannedGrainTexture.value()     : 0.3f;
                float fadeBars    = desc.plannedFxFadeBars.has_value()       ? (float)desc.plannedFxFadeBars.value()       : 1.0f;
                strip.getMutableFxParams().grainDensityHz   = density;
                strip.getMutableFxParams().grainSizeMs      = size;
                strip.getMutableFxParams().grainPitchSpread = pitchSpread;
                strip.getMutableFxParams().grainTexture     = texture;
                strip.setGrainMixEnvelope(strip.getFxParams().grainMix, targetMix, fadeBars);
            }
        }
    }

    void applyBufferGranularMomentary(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                float targetMix   = desc.plannedGrainMix.has_value()         ? (float)desc.plannedGrainMix.value()         : 0.75f;
                float density     = desc.plannedGrainDensityHz.has_value()   ? (float)desc.plannedGrainDensityHz.value()   : 8.0f;
                float size        = desc.plannedGrainSizeMs.has_value()      ? (float)desc.plannedGrainSizeMs.value()      : 150.0f;
                float pitchSpread = desc.plannedGrainPitchSpread.has_value() ? (float)desc.plannedGrainPitchSpread.value() : 0.0f;
                float texture     = desc.plannedGrainTexture.has_value()     ? (float)desc.plannedGrainTexture.value()     : 0.3f;
                double totalBars  = desc.plannedFxFadeBars.value_or(4.0);
                float half = (float)juce::jmax(0.0001, totalBars * 0.5);
                strip.startTemporaryGranular(targetMix, half, 0.0f, half);
                strip.getMutableFxParams().grainDensityHz   = density;
                strip.getMutableFxParams().grainSizeMs      = size;
                strip.getMutableFxParams().grainPitchSpread = pitchSpread;
                strip.getMutableFxParams().grainTexture     = texture;
            }
        }
    }

    void applyBufferDuckingOn(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.getMutableFxParams().duckingEnabled = true;
                strip.getMutableFxParams().duckAmount = 0.5f;      // moderate default
                strip.getMutableFxParams().duckReleaseMs = 250.0f;  // musical release
            }
        }
    }

    void applyBufferDuckingOff(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.getMutableFxParams().duckingEnabled = false;
            }
        }
    }

    void applySwapModifierStack(juce::Array<int> targets)
    {
        // ModifierScheduler supplies the final live selection, so the pips
        // match the stacks that are actually rotated.
        if (targets.size() < 2) return;

        // 1. Capture a BufferModifierSnapshot for each target
        std::vector<BufferModifierSnapshot> snapshots(static_cast<size_t>(targets.size()));
        std::vector<ChannelStrip::ModifierRuntimeState> stripStates(static_cast<size_t>(targets.size()));
        for (int t = 0; t < targets.size(); ++t)
        {
            int idx = targets[t];
            auto& snap = snapshots[static_cast<size_t>(t)];
            auto* buf = bufferManager.getBuffer(idx);
            auto* strip = (juce::isPositiveAndBelow(idx, channelStrips.size())) ? channelStrips[idx] : nullptr;
            if (buf == nullptr || strip == nullptr) continue;

            stripStates[static_cast<size_t>(t)] = strip->captureModifierRuntimeState();

            // AudioBuffer transform state
            snap.speed                   = buf->getSpeed();
            snap.stretchRatio            = buf->getStretchRatio();
            snap.pitchSemiTones          = buf->getPitchSemiTones();
            snap.continuousRandomSlicing = buf->isInContinuousRandomMode();
            snap.numSlices               = buf->getNumSlices();
            snap.arpSliceActive          = buf->isInArpSliceMode() && !buf->isInSliceRepeaterMode();
            snap.arpSliceRepeaterMode    = buf->isInSliceRepeaterMode();
            snap.arpSequenceLength       = buf->getArpSequenceLength();
            snap.arpRepeatBars           = buf->getArpRepeatBars();
            snap.arpTotalSlices          = buf->getArpTotalSlices();
            snap.pingPongEnabled         = buf->isPingPongModeEnabled();
            snap.pingPongDivision        = buf->getPingPongDivision();

            // ChannelStrip FX enable flags
            const auto& fx = strip->effects();
            snap.delayEnabled      = fx.delayEnabled;
            snap.reverbEnabled     = fx.reverbEnabled;
            snap.lowPassEnabled    = fx.lowPassEnabled;
            snap.highPassEnabled   = fx.highPassEnabled;
            snap.tremoloEnabled    = fx.tremoloEnabled;
            snap.chorusEnabled     = fx.chorusEnabled;
            snap.autoPanEnabled    = fx.autoPanEnabled;
            snap.volumeRampEnabled = fx.volumeRampEnabled;
            snap.shLowPassEnabled  = fx.shLowPassEnabled;
            snap.shHighPassEnabled = fx.shHighPassEnabled;
            snap.granularEnabled   = fx.granularEnabled;

            // ChannelStrip FxParams
            const auto& fp = strip->getFxParams();
            snap.reverbWet              = fp.reverbWet;
            snap.reverbPreDelayMs       = fp.reverbPreDelayMs;
            snap.delayFeedback          = fp.delayFeedback;
            snap.delayTimeMs            = fp.delayTimeMs;
            snap.delayWet               = fp.delayWet;
            snap.delayPingPong          = fp.delayPingPong;
            snap.delayFeedbackHighCutHz = fp.delayFeedbackHighCutHz;
            snap.delayFbDrive           = fp.delayFbDrive;
            snap.duckingEnabled         = fp.duckingEnabled;
            snap.duckAmount             = fp.duckAmount;
            snap.duckReleaseMs          = fp.duckReleaseMs;
            snap.wowFlutterEnabled      = fp.wowFlutterEnabled;
            snap.wowDepthMs             = fp.wowDepthMs;
            snap.wowRateHz              = fp.wowRateHz;
            snap.flutterDepthMs         = fp.flutterDepthMs;
            snap.flutterRateHz          = fp.flutterRateHz;
            snap.wowPeriodBars          = fp.wowPeriodBars;
            snap.flutterPeriodBars      = fp.flutterPeriodBars;
            snap.lowPassCutoff          = fp.lowPassCutoff;
            snap.highPassCutoff         = fp.highPassCutoff;
            snap.tremoloDepth           = fp.tremoloDepth;
            snap.tremoloRateHz          = fp.tremoloRateHz;
            snap.chorusDepth            = fp.chorusDepth;
            snap.chorusRateHz           = fp.chorusRateHz;
            snap.chorusMix              = fp.chorusMix;
            snap.chorusDelayMs          = fp.chorusDelayMs;
            snap.panRateHz              = fp.panRateHz;
            snap.panDepth               = fp.panDepth;
            snap.panMix                 = fp.panMix;
            snap.panPeriodBars          = fp.panPeriodBars;
            snap.volumeGain             = fp.volumeGain;
            snap.shLpfCutoff            = fp.shLpfCutoff;
            snap.shLpfQ                 = fp.shLpfQ;
            snap.shHpfCutoff            = fp.shHpfCutoff;
            snap.shHpfQ                 = fp.shHpfQ;
            snap.shDivisionBars         = fp.shDivisionBars;
            snap.grainDensityHz         = fp.grainDensityHz;
            snap.grainSizeMs            = fp.grainSizeMs;
            snap.grainPitchSpread       = fp.grainPitchSpread;
            snap.grainMix               = fp.grainMix;
            snap.grainTexture           = fp.grainTexture;
        }

        // 2. Rotate: buffer[i] receives the snapshot from buffer[i-1],
        //    and buffer[0] receives the snapshot from the last buffer.
        //    For 2 buffers this is a simple swap.
        const auto n = static_cast<int>(snapshots.size());
        std::vector<BufferModifierSnapshot> rotated(static_cast<size_t>(n));
        std::vector<ChannelStrip::ModifierRuntimeState> rotatedStripStates(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            rotated[static_cast<size_t>(i)] = snapshots[static_cast<size_t>((i + n - 1) % n)];
            rotatedStripStates[static_cast<size_t>(i)]
                = stripStates[static_cast<size_t>((i + n - 1) % n)];
        }

        // 3. Restore each target from its new (rotated) snapshot
        for (int t = 0; t < targets.size(); ++t)
        {
            int idx = targets[t];
            const auto& snap = rotated[static_cast<size_t>(t)];
            auto* buf = bufferManager.getBuffer(idx);
            auto* strip = (juce::isPositiveAndBelow(idx, channelStrips.size())) ? channelStrips[idx] : nullptr;
            if (buf == nullptr || strip == nullptr) continue;

            // AudioBuffer transform state
            buf->setSpeed(snap.speed);
            buf->setStretchRatio(snap.stretchRatio);
            buf->setPitchSemiTones(snap.pitchSemiTones);
            if (snap.arpSliceRepeaterMode)
                buf->startSliceRepeater(snap.arpTotalSlices, snap.arpSequenceLength);
            else if (snap.arpSliceActive)
                buf->startArpSlicing(snap.arpTotalSlices, snap.arpSequenceLength, snap.arpRepeatBars);
            else if (snap.continuousRandomSlicing)
            {
                buf->setNumSlices(snap.numSlices);
                buf->startContinuousRandomSlicing();
            }
            else
                buf->exitSlicingMode();

            buf->setPingPongMode(snap.pingPongEnabled, snap.pingPongDivision,
                                 settings.bpm, buf->getFileSampleRate());

            // Move the full FX runtime — including temporary rise/fall/hold
            // controllers — so audio behavior and pad stickers stay aligned.
            strip->restoreModifierRuntimeState(
                rotatedStripStates[static_cast<size_t>(t)]);

            if (buf->hasAudioLoaded() && !buf->isPlaying())
                buf->play();
        }
    }

public:
    // Recompute tempo-synced LFO rates (tremolo, wow/flutter) when BPM changes
    void resyncTempoLFOs()
    {
        const double spb  = settings.getSecondsPerBeat();
        const double spbar = settings.getSecondsPerBar();
        for (int i = 0; i < channelStrips.size(); ++i)
        {
            auto& strip = *channelStrips[i];
            // Tremolo: keep at 1/8-note
            if (strip.effects().tremoloEnabled)
            {
                double rateHz = spb > 0.0 ? (2.0 / spb) : 4.0; // 1/8-note
                strip.getMutableFxParams().tremoloRateHz = (float) rateHz;
            }
            // Wow/Flutter: recompute from stored periods if set (fallback to defaults)
            if (strip.getFxParams().wowFlutterEnabled)
            {
                const float wowBars = strip.getFxParams().wowPeriodBars > 0.0f ? strip.getFxParams().wowPeriodBars : 4.0f;
                const float flutterBars = strip.getFxParams().flutterPeriodBars > 0.0f ? strip.getFxParams().flutterPeriodBars : 1.0f;
                const double wowHz = spbar > 0.0 ? (1.0 / (spbar * wowBars)) : 0.25;
                const double flutterHz = spbar > 0.0 ? (1.0 / (spbar * flutterBars)) : 0.5;
                strip.getMutableFxParams().wowRateHz = (float) wowHz;
                strip.getMutableFxParams().flutterRateHz = (float) flutterHz;
            }
            // Auto-pan: recompute rateHz from stored periodBars
            if (strip.effects().autoPanEnabled && strip.getFxParams().panPeriodBars > 0.0f)
            {
                const double rateHz = spbar > 0.0 ? (1.0 / (spbar * strip.getFxParams().panPeriodBars)) : 2.0;
                strip.getMutableFxParams().panRateHz = (float) rateHz;
            }
        }
    }
    // Advance FX envelopes per audio block. Call with blockSeconds from audio thread owner.
    void advanceFxEnvelopes(double blockSeconds)
    {
        double secondsPerBar = settings.getSecondsPerBar();
        if (secondsPerBar <= 0.0) secondsPerBar = 1.0;
        float barsDelta = (float)(blockSeconds / secondsPerBar);
        for (int i = 0; i < channelStrips.size(); ++i)
        {
            channelStrips[i]->advanceEnvelopes(barsDelta);
            refreshActiveModifierStickerMask(i);
        }
    }
};
