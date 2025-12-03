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
    // Parts API
    void setActivePart(int partIndex)
    {
        settings.parts.activePart = juce::jlimit(0, settings.parts.getNumParts() - 1, partIndex);
        // Compute global start offset in samples and apply to buffer manager
        const int startBar = settings.parts.getPartStartBar(settings.parts.activePart);
        const double secondsPerBar = settings.getSecondsPerBar();
        const double startSeconds = secondsPerBar * (double) startBar;
    const int64_t startSamples = (int64_t) std::round(startSeconds * bufferManager.getHostSampleRate());
        bufferManager.setStartOffsetSamples(startSamples);
    }
    int getActivePart() const { return settings.parts.activePart; }
    int getPartLengthBars() const { return settings.parts.partLengthBars; }
    int getPartStartBar(int partIndex) const { return settings.parts.getPartStartBar(partIndex); }


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
            case ModifierType::SwitchPart:
            {
                // Choose a different part than current and switch immediately
                int num = settings.parts.getNumParts();
                if (num >= 1)
                {
                    int current = settings.parts.activePart;
                    int target = current;
                    if (num == 1)
                        target = 0; // only one part -> A
                    else
                    {
                        // Pick a different index
                        juce::Random r;
                        for (int attempt = 0; attempt < 8; ++attempt)
                        {
                            int cand = r.nextInt(num);
                            if (cand != current) { target = cand; break; }
                        }
                        if (target == current)
                            target = (current + 1) % num; // deterministic fallback
                    }
                    setActivePart(target);
                }
                break;
            }
            // Ducking is enabled by default; no explicit modifier trigger.
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
        }
    }
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
