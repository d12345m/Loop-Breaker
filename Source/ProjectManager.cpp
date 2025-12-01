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
    auto getOr = [obj](const juce::Identifier& id, const juce::var& fallback) -> juce::var
    {
        return obj->hasProperty(id) ? obj->getProperty(id) : fallback;
    };

    settings.projectName         = getOr("name", settings.projectName).toString();
    settings.projectId           = getOr("id", settings.projectId).toString();
    settings.bpm                 = (double)getOr("bpm", settings.bpm);
    settings.timeSigNumerator    = (int)getOr("tsNum", settings.timeSigNumerator);
    settings.timeSigDenominator  = (int)getOr("tsDen", settings.timeSigDenominator);
    settings.barsBetweenModifiers= (int)getOr("barsBetweenModifiers", settings.barsBetweenModifiers);
    settings.quantizeEnabled     = (bool)getOr("quantizeEnabled", settings.quantizeEnabled);
    settings.quantizeSubdivision = (int)getOr("quantizeSubdivision", settings.quantizeSubdivision);
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
    obj->setProperty("quantizeEnabled", settings.quantizeEnabled);
    obj->setProperty("quantizeSubdivision", settings.quantizeSubdivision);

    juce::String json = juce::JSON::toString(juce::var(obj.get()), true);
    return file.replaceWithText(json);
}

bool ProjectManager::renameProject(const juce::String& newName)
{
    if (newName.isEmpty()) return false;
    settings.projectName = newName;
    return true;
}
