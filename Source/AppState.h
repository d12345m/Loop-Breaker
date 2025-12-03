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
                applyBufferReverbOn(desc, targets);
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
                applyBufferDelayOn(desc, targets);
                break;
            case ModifierType::BufferDelayOff:
                applyBufferDelayOff(targets);
                break;
            case ModifierType::BufferDelayPingPongOn:
                applyBufferDelayPingPong(targets, true);
                break;
            case ModifierType::BufferDelayPingPongOff:
                applyBufferDelayPingPong(targets, false);
                break;
            case ModifierType::BufferDelayDubBurst:
                applyBufferDelayDubBurst(desc, targets);
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

    void applyBufferReverbOn(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().reverbEnabled = true;
                // If a planned wet is provided, use it; else default to 0.85
                float targetWet = desc.plannedWet.has_value() ? (float)desc.plannedWet.value() : 0.85f;
                // Use a consistent ramp duration for stability
                float durationBars = desc.plannedWet.has_value() ? 1.0f : 2.0f;
                strip.setReverbWetEnvelope(strip.getFxParams().reverbWet, targetWet, durationBars);
                // Standardize pre-delay: fixed 20 ms for all reverb states to avoid jumping
                strip.setReverbPreDelayEnvelope(strip.getFxParams().reverbPreDelayMs, 20.0f, 0.5f);
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

    void applyBufferDelayOn(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
                strip.effects().delayEnabled = true;
                // Set delay time to one beat length from settings
                double beatMs = settings.getSecondsPerBeat() * 1000.0;
                // Clamp to max defined in ChannelStrip
                strip.getFxParams(); // ensure params accessed (unused result suppressed by ignore)
                // Direct param assignment (no envelope for time yet)
                // Provide simple scaling: if BPM fast (<90) extend to 1.5 beats for more space
                double bpm = settings.bpm;
                double targetMs = (bpm < 90.0 ? beatMs * 1.5 : beatMs);
                juce::Array<float> tapTimesMs;
                auto mapDivisionToMult = [](const juce::String& label)->double{
                    if (label == "1/4")  return 1.0;
                    if (label == "1/8")  return 0.5;
                    if (label == "1/8D") return 0.75;
                    if (label == "1/8T") return 1.0/3.0;
                    if (label == "1/16") return 0.25;
                    if (label == "1/32") return 0.125;
                    return 1.0;
                };
                if (!desc.plannedDelayDivisions.isEmpty())
                {
                    for (auto d : desc.plannedDelayDivisions)
                    {
                        double mult = mapDivisionToMult(d);
                        tapTimesMs.add((float) juce::jlimit(1.0, 2000.0, beatMs * mult));
                    }
                    // Fallback: first division defines primary param for single-tap compatibility
                    if (tapTimesMs.size() > 0)
                        targetMs = tapTimesMs[0];
                    strip.setDelayTapTimesMs(tapTimesMs);
                }
                else if (desc.plannedDelayDivision.isNotEmpty())
                {
                    double mult = mapDivisionToMult(desc.plannedDelayDivision);
                    targetMs = beatMs * mult;
                    tapTimesMs.add((float) juce::jlimit(1.0, 2000.0, targetMs));
                    strip.setDelayTapTimesMs(tapTimesMs);
                }
                else
                {
                    // No divisions specified; ensure previous multi-tap cleared
                    strip.setDelayTapTimesMs(juce::Array<float>());
                }
                // Delay wet override if provided
                if (desc.plannedDelayWet.has_value())
                {
                    strip.getMutableFxParams().delayWet = (float) juce::jlimit(0.0, 1.0, desc.plannedDelayWet.value());
                }
                if (desc.plannedDelayFeedback.has_value())
                {
                    // Immediate set rather than envelope if explicit feedback variant provided
                    strip.getMutableFxParams().delayFeedback = (float) juce::jlimit(0.0, 0.95, desc.plannedDelayFeedback.value());
                }
                // Update params directly
                // (Would add setter if encapsulation demanded)
                strip.advanceEnvelopes(0.0f); // noop ensuring structure
                strip.effects().delayEnabled = true;
                // Ramp feedback to 0.35 over 2 bars for audible repeats
                if (!desc.plannedDelayFeedback.has_value())
                    strip.setDelayFeedbackEnvelope(strip.getFxParams().delayFeedback, 0.35f, 2.0f);
                // If no explicit wet variant, use default baseline
                if (!desc.plannedDelayWet.has_value())
                    strip.getMutableFxParams().delayWet = 0.40f; // baseline
                strip.getMutableFxParams().delayTimeMs = (float) juce::jlimit(1.0, 2000.0, targetMs);
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

    void applyBufferDelayDubBurst(const ModifierDescriptor& desc, const juce::Array<int>& targets)
    {
        if (targets.isEmpty()) return;
        // Dub burst: ramp feedback up quickly, then down to zero, auto-disable after tail
        for (int idx : targets)
        {
            if (juce::isPositiveAndBelow(idx, channelStrips.size()))
            {
                auto& strip = *channelStrips[idx];
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
                strip.getMutableFxParams().delayWet = (float) juce::jlimit(0.0, 1.0, desc.plannedDelayWet.value_or(0.80));
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
