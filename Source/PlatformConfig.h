#pragma once

#include <JuceHeader.h>

namespace LoopBreakerConfig
{
   #if JUCE_IOS
    inline constexpr int numPads = 6;
    inline constexpr int numModifierPresets = 4;
    // The iOS build is a foreground performance instrument rather than one
    // editor among many in a DAW, so favor a smoother visual cadence.
    inline constexpr int uiRefreshRateHz = 30;
   #else
    inline constexpr int numPads = 8;
    inline constexpr int numModifierPresets = 8;
    inline constexpr int uiRefreshRateHz = 15;
   #endif

    inline constexpr double uiRefreshIntervalSeconds =
        1.0 / static_cast<double> (uiRefreshRateHz);
}
