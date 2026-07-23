#pragma once

#include <JuceHeader.h>

namespace UiGridLayout
{
inline juce::Rectangle<int> equalColumn (juce::Rectangle<int> area,
                                         int column,
                                         int columnCount) noexcept
{
    jassert (columnCount > 0);
    jassert (juce::isPositiveAndBelow (column, columnCount));

    const int left = area.getX() + area.getWidth() * column / columnCount;
    const int right = area.getX() + area.getWidth() * (column + 1) / columnCount;
    return { left, area.getY(), right - left, area.getHeight() };
}
}
