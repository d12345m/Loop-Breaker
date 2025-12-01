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

struct AppState : public ModifierSchedulerListener
{
    ProjectManager projectManager;
    AudioBufferManager bufferManager; // existing engine
    SessionSettings& settings = projectManager.getMutableSettings();
    ModifierScheduler scheduler { settings };

    // Channel strips for FX placeholder wrapping existing buffers
    juce::OwnedArray<ChannelStrip> channelStrips;

    AppState()
    {
        // Initialize channel strips referencing underlying buffers
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            channelStrips.add(new ChannelStrip(bufferManager.getBuffer(i)));

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

    void modifierTriggered(const ModifierDescriptor& desc, const juce::Array<int>& targets) override
    {
        switch (desc.type)
        {
            case ModifierType::Reverse:
                applyReverse(targets);
                break;
            case ModifierType::Speed:
                applySpeed(desc, targets);
                break;
            case ModifierType::ResetAll:
                applyReset(targets);
                break;
            case ModifierType::BeatSliceRandom:
                applyBeatSliceRandom(desc, targets);
                break;
            case ModifierType::BufferReverbOn:
                applyBufferReverbOn(targets);
                break;
            case ModifierType::BufferReverbWet25:
                applyBufferReverbWet(targets, 0.25f);
                break;
            case ModifierType::BufferReverbWet50:
                applyBufferReverbWet(targets, 0.50f);
                break;
            case ModifierType::BufferReverbWet75:
                applyBufferReverbWet(targets, 0.75f);
                break;
            case ModifierType::BufferReverbWet100:
                applyBufferReverbWet(targets, 1.00f);
                break;
            case ModifierType::BufferReverbOff:
                applyBufferReverbOff(targets);
                break;
            case ModifierType::BufferDelayOn:
                applyBufferDelayOn(targets);
                break;
            case ModifierType::BufferDelayOff:
                applyBufferDelayOff(targets);
                break;
            case ModifierType::BufferLowPassOn:
                applyBufferLowPassOn(targets);
                break;
            case ModifierType::BufferLowPassOff:
                applyBufferLowPassOff(targets);
                break;
            case ModifierType::BufferHighPassOn:
                applyBufferHighPassOn(targets);
                break;
            case ModifierType::BufferHighPassOff:
                applyBufferHighPassOff(targets);
                break;
            case ModifierType::BufferTremolo:
                applyBufferTremoloOn(targets);
                break;
            case ModifierType::BufferTremoloOff:
                applyBufferTremoloOff(targets);
                break;
            default:
                break; // Unimplemented modifiers ignored for now
        }
    }
private:
    void applyReverse(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                double s = b->getSpeed();
                if (s == 0.0) s = 1.0;
                b->setSpeed(-std::abs(s));
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applySpeed(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        // Use structured planned speed if provided; else default to 1.0 (no change)
        double speedVal = desc.plannedSpeed.has_value() ? desc.plannedSpeed.value() : 1.0;
        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                b->setSpeed(speedVal);
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
            if (auto* b = bufferManager.getBuffer(idx))
            {
                bool wasPlaying = playing.contains(idx);
                b->resetToDefaults();
                b->resetToBeginning();
                if (wasPlaying) b->play();
            }
        }
    }

    void applyBeatSliceRandom(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        struct Division { juce::String name; double factorPerBeat; };
        static const Division divisions[] {
            {"1/4", 1.0}, {"1/8", 2.0}, {"1/8T", 3.0}, {"1/16", 4.0}, {"1/32", 8.0}, {"1/64", 16.0 }
        };
        Division chosen {"1/8", 2.0};
        juce::String label = desc.plannedSliceDivision;
        if (label.isNotEmpty())
        {
            for (auto& d : divisions)
                if (label == d.name) { chosen = d; break; }
        }
        double beatsPerBar = settings.getBeatsPerBar();
        double secondsPerBar = settings.getSecondsPerBar();
        for (int idx : targets)
        {
            if (auto* b = bufferManager.getBuffer(idx); b && b->hasAudioLoaded())
            {
                double durSeconds = b->getDurationInSeconds();
                if (durSeconds <= 0.0) continue;
                double approxBars = durSeconds / secondsPerBar;
                double barsFactor = juce::jmax(1.0, approxBars);
                double slicesD = beatsPerBar * chosen.factorPerBeat * barsFactor;
                int slices = juce::jlimit(1, 64, (int)std::round(slicesD));
                if (slices < 2) continue;
                b->setNumSlices(slices);
                b->startContinuousRandomSlicing();
                if (!b->isPlaying()) b->play();
            }
        }
    }

    void applyBufferReverbOn(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().reverbEnabled = true;
                // Ramp reverb wet up to 0.85 over 2 bars (more audible)
                strip.setReverbWetEnvelope(strip.getFxParams().reverbWet, 0.85f, 2.0f);
            }
        }
    }

    void applyBufferReverbWet(const juce::Array<int>& targets, float targetWet)
    {
        if (targets.isEmpty()) return;
        targetWet = juce::jlimit(0.0f, 1.0f, targetWet);
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().reverbEnabled = true;
                // Ramp to requested wet over 1 bar for snappy response
                strip.setReverbWetEnvelope(strip.getFxParams().reverbWet, targetWet, 1.0f);
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

    void applyBufferDelayOn(const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().delayEnabled = true;
                // Ramp delay feedback to 0.25 over 2 bars
                strip.setDelayFeedbackEnvelope(strip.getFxParams().delayFeedback, 0.25f, 2.0f);
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
                strip.setDelayFeedbackEnvelope(strip.getFxParams().delayFeedback, 0.0f, 2.0f);
                strip.effects().delayEnabled = true; // keep on during ramp
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

public:
    // Advance FX envelopes per audio block. Call with blockSeconds from audio thread owner.
    void advanceFxEnvelopes(double blockSeconds)
    {
        double secondsPerBar = settings.getSecondsPerBar();
        if (secondsPerBar <= 0.0) secondsPerBar = 1.0;
        float barsDelta = (float)(blockSeconds / secondsPerBar);
        for (int i = 0; i < channelStrips.size(); ++i)
            channelStrips[i]->advanceEnvelopes(barsDelta);
    }
};
