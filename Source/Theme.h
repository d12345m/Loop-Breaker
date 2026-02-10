#pragma once

#include <JuceHeader.h>

namespace Theme
{
inline juce::Colour bg()            { return juce::Colour::fromRGB(245, 247, 250); } // app background
inline juce::Colour panel()         { return juce::Colour::fromRGB(255, 255, 255); } // cards
inline juce::Colour panelAlt()      { return juce::Colour::fromRGB(250, 251, 253); }
inline juce::Colour border()        { return juce::Colour::fromRGB(220, 226, 234); }
inline juce::Colour borderStrong()  { return juce::Colour::fromRGB(200, 208, 218); }

inline juce::Colour text()          { return juce::Colour::fromRGB(28, 32, 38); }
inline juce::Colour textSubtle()    { return juce::Colour::fromRGB(92, 100, 112); }

inline juce::Colour accent()        { return juce::Colour::fromRGB(88, 101, 242); } // periwinkle
inline juce::Colour accent2()       { return juce::Colour::fromRGB(20, 184, 166); } // teal
inline juce::Colour good()          { return juce::Colour::fromRGB(34, 197, 94); }
inline juce::Colour warn()          { return juce::Colour::fromRGB(245, 158, 11); }
inline juce::Colour bad()           { return juce::Colour::fromRGB(239, 68, 68); }

inline juce::Colour overlay(float a){ return juce::Colours::black.withAlpha(juce::jlimit(0.0f, 1.0f, a)); }
}
