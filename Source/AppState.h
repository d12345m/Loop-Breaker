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
    juce::Array<ChannelStrip> channelStrips;

    AppState()
    {
        // Initialize channel strips referencing underlying buffers
        for (int i = 0; i < AudioBufferManager::MAX_BUFFERS; ++i)
            channelStrips.add(ChannelStrip(bufferManager.getBuffer(i)));

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
        // Parse planned speed value from description after '->'
        double speedVal = 1.0;
        auto text = desc.description;
        int arrow = text.lastIndexOf("->");
        if (arrow >= 0)
        {
            auto part = text.substring(arrow + 2).trim(); // e.g. "0.50x"
            if (part.endsWithIgnoreCase("x"))
                part = part.dropLastCharacters(1);
            double v = part.getDoubleValue();
            if (v > 0.0) speedVal = v;
        }
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
        auto dd = desc.description;
        int arrow = dd.lastIndexOf("->");
        if (arrow >= 0)
        {
            auto label = dd.substring(arrow + 2).trim();
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
};
