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
        // Apply immediate, stateless modifier effects to targeted buffers.
        switch (desc.type)
        {
            case ModifierType::Reverse:
            {
                auto applyReverse = [&](AudioBuffer* b)
                {
                    if (!b) return;
                    double current = b->getSpeed();
                    if (current == 0.0) current = 1.0; // avoid zero speed flip
                    b->setSpeed(-current); // flipping sign reverses playback direction
                };
                if (targets.isEmpty())
                {
                    // If no explicit targets, treat as global buffer transform: apply to all loaded
                    auto loaded = bufferManager.getLoadedBufferIndices();
                    for (int idx : loaded) applyReverse(bufferManager.getBuffer(idx));
                }
                else
                {
                    for (int idx : targets) applyReverse(bufferManager.getBuffer(idx));
                }
                break;
            }
            case ModifierType::Speed:
            {
                // Choose a random discrete speed per trigger (shared for all targets) for musical cohesion
                static const double speeds[] { 0.25, 0.5, 1.0, 2.0 };
                juce::Random r;
                double newSpeed = speeds[r.nextInt((int)std::size(speeds))];
                auto applySpeed = [&](AudioBuffer* b)
                {
                    if (!b) return;
                    double sign = b->getSpeed() < 0.0 ? -1.0 : 1.0; // preserve direction
                    b->setSpeed(sign * newSpeed);
                };
                if (targets.isEmpty())
                {
                    auto loaded = bufferManager.getLoadedBufferIndices();
                    for (int idx : loaded) applySpeed(bufferManager.getBuffer(idx));
                }
                else
                {
                    for (int idx : targets) applySpeed(bufferManager.getBuffer(idx));
                }
                break;
            }
            case ModifierType::ResetAll:
            {
                // Reset only targeted buffers (now pad-specific). If no targets array provided, scheduler
                // has already randomized targets; we thus expect non-empty list or treat empty as no-op.
                if (targets.isEmpty()) break;
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
                break;
            }
            default:
                break; // Other types not yet implemented
        }
    }
};
