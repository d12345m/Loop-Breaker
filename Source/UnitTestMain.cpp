#include <JuceHeader.h>
#include "GlyphContactSheet.h"
#include "ThemeEngine.h"

#include <iostream>

namespace
{
int runUnitTests()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("LoopBreaker", 0x4c4f4f50);

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
            failures += result->failures;

    return failures == 0 ? 0 : 1;
}

int exportGlyphSheet (const juce::String& requestedPath)
{
    const auto destination = juce::File::isAbsolutePath (requestedPath)
                           ? juce::File (requestedPath)
                           : juce::File::getCurrentWorkingDirectory().getChildFile (requestedPath);
    const auto palette = ControlSurfacePalette::fromTheme (
        ThemeEngine::getInstance().getCurrentPalette());

    if (! GlyphContactSheet::exportPng (destination, palette))
    {
        std::cerr << "Failed to export glyph contact sheet to "
                  << destination.getFullPathName() << '\n';
        return 1;
    }

    std::cout << "Exported glyph contact sheet to "
              << destination.getFullPathName() << '\n';
    return 0;
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 1)
        return runUnitTests();

    if (argc == 3 && juce::String (argv[1]) == "--export-glyph-sheet")
        return exportGlyphSheet (juce::String::fromUTF8 (argv[2]));

    std::cerr << "Usage: " << argv[0]
              << " [--export-glyph-sheet <output.png>]\n";
    return 2;
}
