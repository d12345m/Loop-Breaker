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
        juce::ignoreUnused(desc, targets);
        // Future: enqueue actual modifier processing / apply to FX or buffers
    }
};
