/*
 ============================================================================== 
   ProjectManager.cpp
   --------------------------------------------------------------------------
   Placeholder persistence logic – writes simple JSON file (future expansion).
 ==============================================================================
*/

#include "ProjectManager.h"

ProjectManager::ProjectManager() = default;

bool ProjectManager::newProject(const juce::String& name)
{
    settings.projectName = name;
    settings.projectId = juce::Uuid().toString();
    return true;
}

bool ProjectManager::loadProject(const juce::File& file)
{
    if (!file.existsAsFile()) return false;
    juce::var parsed = juce::JSON::parse(file);
    if (parsed.isVoid() || !parsed.isObject()) return false;
    auto* obj = parsed.getDynamicObject();
    if (!obj) return false;
    settings.projectName = obj->getProperty("name", settings.projectName).toString();
    settings.projectId = obj->getProperty("id", settings.projectId).toString();
    settings.bpm = (double)obj->getProperty("bpm", settings.bpm);
    settings.timeSigNumerator = (int)obj->getProperty("tsNum", settings.timeSigNumerator);
    settings.timeSigDenominator = (int)obj->getProperty("tsDen", settings.timeSigDenominator);
    settings.barsBetweenModifiers = (int)obj->getProperty("barsBetweenModifiers", settings.barsBetweenModifiers);
    return true;
}

bool ProjectManager::saveProject(const juce::File& directory, bool overwrite) const
{
    if (!directory.createDirectory())
        return false;
    juce::File file = directory.getChildFile(settings.projectName + ".json");
    if (file.existsAsFile() && !overwrite)
        return false;

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("name", settings.projectName);
    obj->setProperty("id", settings.projectId);
    obj->setProperty("bpm", settings.bpm);
    obj->setProperty("tsNum", settings.timeSigNumerator);
    obj->setProperty("tsDen", settings.timeSigDenominator);
    obj->setProperty("barsBetweenModifiers", settings.barsBetweenModifiers);

    juce::String json = juce::JSON::toString(juce::var(obj), true);
    return file.replaceWithText(json);
}

bool ProjectManager::renameProject(const juce::String& newName)
{
    if (newName.isEmpty()) return false;
    settings.projectName = newName;
    return true;
}
