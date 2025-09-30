/*
 ============================================================================== 
   ProjectManager.h
   --------------------------------------------------------------------------
   Handles creation / loading / saving of projects (skeleton only).
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "SessionSettings.h"

class ProjectManager
{
public:
    ProjectManager();

    bool newProject(const juce::String& name);
    bool loadProject(const juce::File& file);
    bool saveProject(const juce::File& directory, bool overwrite = true) const;
    bool renameProject(const juce::String& newName);

    const SessionSettings& getSettings() const { return settings; }
    SessionSettings& getMutableSettings() { return settings; }

private:
    SessionSettings settings;
};
