#include "ModifierGlyphRenderer.h"
#include "ModifierRegistry.h"

#include <cmath>

namespace
{
struct GlyphCanvas
{
    GlyphCanvas (juce::Graphics& graphics, juce::Rectangle<float> target,
                 const ControlSurfacePalette& colours, float opacity, bool compactMode)
        : g (graphics), p (colours), compact (compactMode),
          alpha (juce::jlimit (0.0f, 1.0f, opacity))
    {
        const float side = juce::jmin (target.getWidth(), target.getHeight());
        bounds = target.withSizeKeepingCentre (side, side).reduced (side * 0.06f);
        unit = bounds.getWidth() / 100.0f;
    }

    float x (float n) const { return bounds.getX() + n * unit; }
    float y (float n) const { return bounds.getY() + n * unit; }
    float u (float n) const { return n * unit; }
    juce::Point<float> pt (float px, float py) const { return { x (px), y (py) }; }
    juce::Rectangle<float> rect (float px, float py, float w, float h) const
    {
        return { x (px), y (py), u (w), u (h) };
    }

    void colour (juce::Colour c, float opacity = 1.0f)
    {
        g.setColour (c.withMultipliedAlpha (alpha * opacity));
    }

    void line (float x1, float y1, float x2, float y2, float width = 2.0f)
    {
        g.drawLine ({ x (x1), y (y1), x (x2), y (y2) },
                    juce::jmax (1.0f, u (width)));
    }

    void stroke (const juce::Path& path, float width = 2.0f)
    {
        g.strokePath (path, juce::PathStrokeType (juce::jmax (1.0f, u (width)),
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    void machineStroke (const juce::Path& path, float width = 2.0f)
    {
        g.strokePath (path, juce::PathStrokeType (juce::jmax (1.0f, u (width)),
                                                  juce::PathStrokeType::mitered,
                                                  juce::PathStrokeType::butt));
    }

    void dot (float px, float py, float radius, juce::Colour c, float opacity = 1.0f)
    {
        colour (c, opacity);
        g.fillEllipse (rect (px - radius, py - radius, radius * 2.0f, radius * 2.0f));
    }

    void ring (float px, float py, float radius, juce::Colour c,
               float width = 2.0f, float opacity = 1.0f)
    {
        colour (c, opacity);
        g.drawEllipse (rect (px - radius, py - radius, radius * 2.0f, radius * 2.0f),
                       juce::jmax (1.0f, u (width)));
    }

    void filledRect (float px, float py, float width, float height, juce::Colour c,
                     float opacity = 1.0f)
    {
        colour (c, opacity);
        g.fillRect (rect (px, py, width, height));
    }

    void outlinedRect (float px, float py, float width, float height, juce::Colour c,
                       float strokeWidth = 2.0f, float opacity = 1.0f)
    {
        colour (c, opacity);
        g.drawRect (rect (px, py, width, height),
                    juce::jmax (1.0f, u (strokeWidth)));
    }

    void terminal (float px, float py, float angle, juce::Colour c,
                   float length = 8.0f, float width = 1.5f)
    {
        colour (c);
        const float dx = std::cos (angle + juce::MathConstants<float>::halfPi) * length * 0.5f;
        const float dy = std::sin (angle + juce::MathConstants<float>::halfPi) * length * 0.5f;
        line (px - dx, py - dy, px + dx, py + dy, width);
    }

    void arrowHead (float px, float py, float angle, float size = 6.0f)
    {
        juce::Path arrow;
        const auto tip = pt (px, py);
        const float a = u (size);
        arrow.startNewSubPath (tip);
        arrow.lineTo (tip.x - std::cos (angle - 0.55f) * a,
                      tip.y - std::sin (angle - 0.55f) * a);
        arrow.lineTo (tip.x - std::cos (angle + 0.55f) * a,
                      tip.y - std::sin (angle + 0.55f) * a);
        arrow.closeSubPath();
        g.fillPath (arrow);
    }

    juce::Graphics& g;
    const ControlSurfacePalette& p;
    juce::Rectangle<float> bounds;
    bool compact = false;
    float unit = 1.0f;
    float alpha = 1.0f;
};

float wrapped (float n)
{
    n = std::fmod (n, 1.0f);
    return n < 0.0f ? n + 1.0f : n;
}

float triangle (float phase)
{
    return 1.0f - std::abs (wrapped (phase) * 2.0f - 1.0f);
}

juce::Path wavePath (const GlyphCanvas& c, float left, float right, float centre,
                     float amplitude, float cycles, float phase = 0.0f)
{
    juce::Path path;
    constexpr int points = 48;
    for (int i = 0; i <= points; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (points);
        const float px = left + (right - left) * t;
        const float py = centre + std::sin ((t * cycles + phase) * juce::MathConstants<float>::twoPi) * amplitude;
        if (i == 0) path.startNewSubPath (c.pt (px, py));
        else        path.lineTo (c.pt (px, py));
    }
    return path;
}

juce::Path irregularArc (const GlyphCanvas& c, float cx, float cy, float radius,
                         float wobble, float phase, float startAngle, float endAngle)
{
    juce::Path path;
    constexpr int points = 64;
    for (int i = 0; i <= points; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (points);
        const float a = startAngle + (endAngle - startAngle) * t;
        const float r = radius
                      + std::sin (a * 3.0f + phase * juce::MathConstants<float>::twoPi) * wobble
                      + std::sin (a * 5.0f - phase * juce::MathConstants<float>::twoPi) * wobble * 0.22f;
        const auto point = c.pt (cx + std::cos (a) * r, cy + std::sin (a) * r);
        if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
    }
    return path;
}

void drawReverse (GlyphCanvas& c, float phase)
{
    const float cycle = wrapped (phase);
    const float shift = std::sin (cycle * juce::MathConstants<float>::twoPi)
                      * (c.compact ? 2.5f : 3.5f);

    // A mechanical return rail anchors the moving chevrons.  The signal lamp
    // travels against the normal left-to-right reading direction.
    c.colour (c.p.mutedInk);
    c.line (17.0f, 76.0f, 84.0f, 76.0f, 1.25f);
    c.terminal (17.0f, 76.0f, 0.0f, c.p.ink, 10.0f, 2.0f);
    c.terminal (84.0f, 76.0f, 0.0f, c.p.ink, 7.0f, 1.4f);

    c.colour (c.p.ink);
    const int toothCount = c.compact ? 2 : 3;
    const float toothStart = c.compact ? 38.0f : 29.0f;
    for (int i = 0; i < toothCount; ++i)
    {
        const float px = toothStart + i * 19.0f + shift;
        juce::Path chevron;
        chevron.startNewSubPath (c.pt (px + 11.0f, 29.0f));
        chevron.lineTo (c.pt (px, 50.0f));
        chevron.lineTo (c.pt (px + 11.0f, 71.0f));
        c.machineStroke (chevron, 3.2f);
    }

    const float lampX = 79.0f - cycle * 56.0f;
    c.dot (lampX, 76.0f, c.compact ? 3.0f : 3.6f, c.p.vermilion);
    c.colour (c.p.vermilion);
    c.arrowHead (14.0f, 76.0f, juce::MathConstants<float>::pi, 4.5f);
}

void drawSpeed (GlyphCanvas& c, float phase, const ModifierDescriptor& descriptor)
{
    const float speed = static_cast<float> (juce::jlimit (0.25, 2.0, descriptor.plannedSpeed.value_or (1.0)));
    const float cycle = wrapped (phase);
    const float breathe = (triangle (cycle) - 0.5f) * (4.0f + speed * 3.0f);
    const float fullPositions[] = { 17.0f, 29.0f + breathe, 43.0f + breathe * 0.35f,
                                    58.0f, 72.0f - breathe * 0.25f, 84.0f };
    const float compactPositions[] = { 22.0f, 50.0f + breathe * 0.25f, 79.0f };
    const int gateCount = c.compact ? 3 : 6;
    const int scanner = static_cast<int> (cycle * static_cast<float> (gateCount)) % gateCount;
    constexpr float tops[] = { 25.0f, 34.0f, 29.0f, 38.0f, 30.0f, 24.0f };
    constexpr float bottoms[] = { 69.0f, 65.0f, 72.0f, 64.0f, 70.0f, 68.0f };

    for (int i = 0; i < gateCount; ++i)
    {
        const float position = c.compact ? compactPositions[i] : fullPositions[i];
        const int dimensionIndex = c.compact ? i * 2 : i;
        c.colour (i == scanner ? c.p.vermilion : c.p.ink,
                  i == scanner ? 1.0f : 0.92f);
        c.line (position, tops[dimensionIndex], position, bottoms[dimensionIndex],
                i == scanner ? 3.0f : 2.0f);
    }

    c.colour (c.p.mutedInk);
    c.line (14.0f, 79.0f, 86.0f, 79.0f, 1.15f);
    c.terminal (14.0f, 79.0f, 0.0f, c.p.ink, 8.0f, 1.5f);
    c.colour (c.p.ink);
    c.arrowHead (89.0f, 79.0f, 0.0f, 4.5f);
    c.dot (17.0f + cycle * 67.0f, 79.0f, c.compact ? 2.4f : 3.0f, c.p.vermilion);
}

void drawStretch (GlyphCanvas& c, float phase, const ModifierDescriptor& descriptor)
{
    const float ratio = static_cast<float> (juce::jlimit (0.25, 2.0, descriptor.plannedStretch.value_or (1.0)));
    const float plannedSpread = juce::jlimit (-8.0f, 8.0f, std::log2 (ratio) * 6.0f);
    const float spread = plannedSpread + 3.0f * triangle (phase);
    const float left = 20.0f - spread;
    const float right = 80.0f + spread;
    c.colour (c.p.ink);
    c.line (left, 20.0f, left, 76.0f, 3.0f);
    c.line (right, 20.0f, right, 76.0f, 3.0f);
    c.line (left, 20.0f, left + 7.0f, 20.0f, 1.7f);
    c.line (left, 76.0f, left + 7.0f, 76.0f, 1.7f);
    c.line (right - 7.0f, 20.0f, right, 20.0f, 1.7f);
    c.line (right - 7.0f, 76.0f, right, 76.0f, 1.7f);
    c.stroke (wavePath (c, left + 7.0f, right - 7.0f, 48.0f, 10.0f, 3.0f), 2.2f);

    c.filledRect (left - 2.2f, 45.0f, 4.4f, 6.0f, c.p.vermilion);
    c.filledRect (right - 2.2f, 45.0f, 4.4f, 6.0f, c.p.vermilion);
    c.colour (c.p.vermilion);
    if (c.compact)
    {
        c.line (left + 4.0f, 84.0f, right - 4.0f, 84.0f, 2.6f);
        c.terminal (left + 4.0f, 84.0f, 0.0f, c.p.vermilion, 6.0f, 1.5f);
        c.terminal (right - 4.0f, 84.0f, 0.0f, c.p.vermilion, 6.0f, 1.5f);
    }
    else
    {
        c.line (48.0f, 84.0f, left + 8.0f, 84.0f, 1.7f);
        c.line (52.0f, 84.0f, right - 8.0f, 84.0f, 1.7f);
        c.arrowHead (left + 3.0f, 84.0f, juce::MathConstants<float>::pi, 4.5f);
        c.arrowHead (right - 3.0f, 84.0f, 0.0f, 4.5f);
    }
    c.dot (50.0f, 84.0f, 2.4f, c.p.ink);
}

void drawPitch (GlyphCanvas& c, float phase, bool up)
{
    const float direction = up ? -1.0f : 1.0f;
    juce::Path steps;
    const int stepCount = c.compact ? 3 : 4;
    const float stepWidth = c.compact ? 20.0f : 16.0f;
    const float stepHeight = c.compact ? 16.0f : 13.0f;
    steps.startNewSubPath (c.pt (18.0f, up ? 72.0f : 28.0f));
    for (int i = 0; i < stepCount; ++i)
    {
        const float nextX = 18.0f + (i + 1) * stepWidth;
        const float y0 = (up ? 72.0f : 28.0f) + direction * i * stepHeight;
        const float y1 = y0 + direction * stepHeight;
        steps.lineTo (c.pt (nextX, y0));
        steps.lineTo (c.pt (nextX, y1));
    }
    c.colour (c.p.ink);
    c.machineStroke (steps, 2.3f);
    constexpr float upperRegisterY = 13.0f;
    constexpr float lowerRegisterY = 87.0f;
    const float registerY = up ? upperRegisterY : lowerRegisterY;
    const float destinationX = 18.0f + stepCount * stepWidth;
    const float destinationY = (up ? 72.0f : 28.0f) + direction * stepCount * stepHeight;
    c.colour (c.p.mutedInk);
    c.line (destinationX, destinationY, destinationX, registerY, 1.4f);
    c.line (destinationX - 17.0f, registerY, destinationX + 4.0f, registerY, 1.2f);
    c.terminal (destinationX + 4.0f, registerY, 0.0f, c.p.ink, 7.0f, 1.4f);
    const float travel = wrapped (phase) * static_cast<float> (stepCount);
    const int step = juce::jlimit (0, stepCount - 1, static_cast<int> (travel));
    const float signalX = 18.0f + stepWidth * (static_cast<float> (step) + 0.5f);
    const float signalY = (up ? 72.0f - stepHeight * 0.5f : 28.0f + stepHeight * 0.5f)
                        + direction * step * stepHeight;
    c.ring (signalX, signalY, c.compact ? 3.2f : 4.2f, c.p.vermilion, 1.5f);
    c.dot (signalX, signalY, c.compact ? 1.7f : 2.1f, c.p.vermilion);
}

void drawArpSlice (GlyphCanvas& c, float phase, const ModifierDescriptor& d)
{
    constexpr int repeatsPerSlice = 4;
    const int sequenceLength = juce::jlimit (1, 8, d.plannedArpSequenceLength.value_or (4));
    const int visibleSlices = juce::jmin (4, sequenceLength);
    const int barCount = visibleSlices * repeatsPerSlice;
    const int active = static_cast<int> (wrapped (phase) * static_cast<float> (barCount)) % barCount;
    constexpr float heights[] = { 25.0f, 51.0f, 35.0f, 59.0f };

    if (c.compact)
    {
        constexpr float groupWidth = 11.0f;
        constexpr float strikeWidth = 4.0f;
        constexpr float strikeGap = 3.0f;
        constexpr float gap = 5.0f;
        const float totalWidth = visibleSlices * groupWidth + (visibleSlices - 1) * gap;
        const float left = 50.0f - totalWidth * 0.5f;
        const int activeGroup = active / repeatsPerSlice;
        for (int group = 0; group < visibleSlices; ++group)
        {
            const float height = heights[group];
            const float groupX = left + group * (groupWidth + gap);
            for (int repeat = 0; repeat < 2; ++repeat)
                c.filledRect (groupX + repeat * (strikeWidth + strikeGap),
                              69.0f - height, strikeWidth, height,
                              group == activeGroup ? c.p.vermilion : c.p.ink,
                              group == activeGroup ? 1.0f : 0.84f);
        }
    }
    else
    {
        constexpr float strikeWidth = 2.15f;
        constexpr float strikeGap = 1.25f;
        constexpr float groupGap = 5.4f;
        constexpr float groupWidth = repeatsPerSlice * strikeWidth
                                   + (repeatsPerSlice - 1) * strikeGap;
        const float totalWidth = visibleSlices * groupWidth
                               + (visibleSlices - 1) * groupGap;
        const float left = 50.0f - totalWidth * 0.5f;
        int strike = 0;
        for (int group = 0; group < visibleSlices; ++group)
        {
            const float height = heights[group];
            const float groupX = left + group * (groupWidth + groupGap);
            for (int repeat = 0; repeat < repeatsPerSlice; ++repeat, ++strike)
            {
                c.filledRect (groupX + repeat * (strikeWidth + strikeGap),
                              69.0f - height, strikeWidth, height,
                              strike == active ? c.p.vermilion : c.p.ink,
                              strike == active ? 1.0f : 0.78f);
            }

            c.colour (c.p.mutedInk, 0.7f);
            c.line (groupX, 73.0f, groupX + groupWidth, 73.0f, 1.0f);
        }
    }

    c.colour (c.p.mutedInk);
    c.line (12.0f, 80.0f, 88.0f, 80.0f, 1.15f);
    c.terminal (12.0f, 80.0f, 0.0f, c.p.ink, 7.0f, 1.4f);
    c.terminal (88.0f, 80.0f, 0.0f, c.p.ink, 7.0f, 1.4f);
}

void drawSliceBlocks (GlyphCanvas& c, float phase, ModifierType type, const ModifierDescriptor& d)
{
    const bool repeatMode = type == ModifierType::SliceRepeater;
    int count = repeatMode ? 4 : 5;
    if (! repeatMode && d.plannedSliceDivision.isNotEmpty())
    {
        const int division = d.plannedSliceDivision.getTrailingIntValue();
        count = division >= 16 ? 7 : (division >= 8 ? 6 : 5);
    }
    if (c.compact && ! repeatMode)
        count = juce::jmin (5, count);

    const float traversal = wrapped (phase) * static_cast<float> (count);
    const int activeStep = static_cast<int> (traversal) % count;
    bool activeVisible = true;
    if (repeatMode)
    {
        const float repeatPhase = traversal - std::floor (traversal);
        activeVisible = static_cast<int> (repeatPhase * 4.0f) % 2 == 0;
    }

    int active = -1;
    if (activeVisible)
    {
        if (repeatMode)
        {
            active = (activeStep * 3 + 1) % count;
        }
        else
        {
            constexpr int fiveSliceOrder[] = { 1, 4, 2, 0, 3 };
            constexpr int sixSliceOrder[] = { 1, 4, 0, 5, 2, 3 };
            constexpr int sevenSliceOrder[] = { 1, 4, 0, 6, 3, 5, 2 };
            const int* order = count == 5 ? fiveSliceOrder
                             : (count == 6 ? sixSliceOrder : sevenSliceOrder);
            active = order[activeStep];
        }
    }

    if (! repeatMode)
    {
        constexpr float heights[] = { 35.0f, 50.0f, 31.0f, 45.0f, 38.0f, 54.0f, 33.0f };
        const float gap = c.compact ? 3.3f : 3.8f;
        const float width = c.compact ? 8.5f : 7.0f;
        const float totalWidth = count * width + (count - 1) * gap;
        const float left = 50.0f - totalWidth * 0.5f;
        for (int i = 0; i < count; ++i)
        {
            const float height = heights[i];
            c.filledRect (left + i * (width + gap), 71.0f - height,
                          width, height,
                          i == active ? c.p.vermilion : c.p.ink,
                          i == active ? 1.0f : 0.78f);
        }

        c.colour (c.p.mutedInk);
        c.line (left - 3.0f, 78.0f, left + totalWidth + 3.0f, 78.0f, 1.1f);
        return;
    }

    constexpr float gap = 4.0f;
    constexpr float width = 15.0f;
    constexpr float height = 34.0f;
    const float totalWidth = count * width + (count - 1) * gap;
    const float left = 50.0f - totalWidth * 0.5f;
    for (int i = 0; i < count; ++i)
    {
        const float x = left + i * (width + gap);
        if (i == active)
        {
            c.filledRect (x, 39.0f, width, height, c.p.vermilion);
            c.filledRect (x + width - 3.5f, 43.0f, 1.5f, height - 8.0f, c.p.raisedTile, 0.92f);
        }
        else
        {
            c.outlinedRect (x, 39.0f, width, height, c.p.ink, 1.7f, 0.86f);
        }
    }

    // The return hook makes the selected packet read as a repeat source, not
    // merely a selection.  Compact mode uses the doubled edge above instead.
    if (! c.compact && active >= 0)
    {
        const float activeLeft = left + active * (width + gap);
        juce::Path hook;
        hook.startNewSubPath (c.pt (activeLeft + width - 3.0f, 34.0f));
        hook.lineTo (c.pt (activeLeft + width - 3.0f, 27.0f));
        hook.lineTo (c.pt (activeLeft + 3.0f, 27.0f));
        hook.lineTo (c.pt (activeLeft + 3.0f, 33.0f));
        c.colour (c.p.vermilion);
        c.machineStroke (hook, 2.0f);
        c.arrowHead (activeLeft + 3.0f, 36.0f, juce::MathConstants<float>::halfPi, 3.8f);
    }

    c.colour (c.p.mutedInk);
    c.line (left - 3.0f, 80.0f, left + totalWidth + 3.0f, 80.0f, 1.1f);
}

void drawPingPong (GlyphCanvas& c, float phase)
{
    // Inward-facing reflectors carry the bounce reading without relying on
    // miniature opposing arrowheads.
    c.colour (c.p.ink);
    c.line (17.0f, 27.0f, 17.0f, 73.0f, 3.0f);
    c.line (83.0f, 27.0f, 83.0f, 73.0f, 3.0f);
    c.line (17.0f, 35.0f, 24.0f, 41.0f, 1.8f);
    c.line (17.0f, 65.0f, 24.0f, 59.0f, 1.8f);
    c.line (83.0f, 35.0f, 76.0f, 41.0f, 1.8f);
    c.line (83.0f, 65.0f, 76.0f, 59.0f, 1.8f);

    c.colour (c.p.mutedInk);
    c.line (24.0f, 50.0f, 76.0f, 50.0f, 1.15f);
    const float signalX = 24.0f + triangle (phase) * 52.0f;
    if (! c.compact)
    {
        const float cycle = wrapped (phase);
        const float direction = cycle < 0.5f ? 1.0f : -1.0f;
        const float turnEnvelope = std::abs (
            std::sin (cycle * juce::MathConstants<float>::twoPi));
        const float availableTrail = direction > 0.0f ? signalX - 24.0f
                                                      : 76.0f - signalX;
        const float trailOffset = juce::jmin (8.0f * turnEnvelope,
                                              juce::jmax (0.0f, availableTrail));
        c.ring (signalX - direction * trailOffset, 50.0f, 1.8f,
                c.p.mutedInk, 1.0f, 0.55f * turnEnvelope);
    }
    c.dot (signalX, 50.0f, c.compact ? 4.0f : 4.8f, c.p.vermilion);
}

void drawDelay (GlyphCanvas& c, float phase, const ModifierDescriptor& descriptor)
{
    constexpr float sourceX = 22.0f;
    constexpr float sourceY = 52.0f;
    const float feedback = static_cast<float> (juce::jlimit (0.0, 1.0,
                                             descriptor.plannedDelayFeedback.value_or (0.65)));
    const float wet = static_cast<float> (juce::jlimit (0.0, 1.0,
                                        descriptor.plannedDelayWet.value_or (0.5)));
    const int echoCount = c.compact ? 2 : (feedback > 0.45f ? 3 : 2);

    c.colour (c.p.mutedInk);
    c.line (sourceX, sourceY, 89.0f, sourceY, 1.0f);
    c.terminal (89.0f, sourceY, 0.0f, c.p.mutedInk, 7.0f, 1.2f);

    for (int i = 0; i < echoCount; ++i)
    {
        const float echoX = c.compact ? 44.0f + i * 25.0f
                                      : 40.0f + i * 18.5f;
        const float halfHeight = 16.0f - i * 3.0f;
        const float bulge = 7.5f - i * 1.0f;
        juce::Path echo;
        echo.startNewSubPath (c.pt (echoX - 3.0f, sourceY - halfHeight));
        echo.cubicTo (c.pt (echoX + bulge, sourceY - halfHeight * 0.68f),
                      c.pt (echoX + bulge, sourceY + halfHeight * 0.68f),
                      c.pt (echoX - 3.0f, sourceY + halfHeight));
        c.colour (i == 0 ? c.p.ink : c.p.mutedInk,
                  (0.62f + feedback * 0.38f) - i * (0.14f + (1.0f - feedback) * 0.08f));
        c.stroke (echo, i == 0 ? 2.3f : 1.65f);

        if (! c.compact)
            c.ring (echoX + bulge, sourceY, 1.9f, c.p.mutedInk, 1.0f,
                    0.75f - i * 0.12f);
    }

    c.colour (c.p.vermilion);
    c.line (sourceX, 39.0f, sourceX, 65.0f, 3.2f);
    c.dot (sourceX, sourceY, 4.2f, c.p.vermilion, 0.72f + wet * 0.28f);
    c.dot (sourceX + wrapped (phase) * 60.0f, sourceY,
           c.compact ? 2.5f : 3.0f, c.p.vermilion, 0.55f + wet * 0.45f);
}

void drawDubDelay (GlyphCanvas& c, float phase, const ModifierDescriptor& descriptor, bool compact)
{
    // A dub sound-system cabinet doubles as a tape machine: the two small
    // upper reels feed a large bass driver, then staggered wavefronts bloom
    // into space.  This keeps the cause-and-effect legible without relying on
    // the red/gold/green echo lamps alone.
    constexpr float cabinetLeft = 13.0f;
    constexpr float cabinetTop = 18.0f;
    constexpr float cabinetWidth = 35.0f;
    constexpr float cabinetHeight = 65.0f;
    constexpr float sourceX = cabinetLeft + cabinetWidth;
    constexpr float wooferY = 57.0f;
    constexpr float waveY = 50.0f;
    const float cycle = wrapped (phase);
    const float bassPulse = std::pow (triangle (cycle * 2.0f), 1.7f);
    const float wet = static_cast<float> (juce::jlimit (0.0, 1.0,
                                        descriptor.plannedDelayWet.value_or (0.65)));
    const float feedback = static_cast<float> (juce::jlimit (0.0, 1.0,
                                             descriptor.plannedDelayFeedback.value_or (0.72)));

    c.colour (c.p.ink);
    c.g.drawRect (c.rect (cabinetLeft, cabinetTop, cabinetWidth, cabinetHeight),
                  juce::jmax (1.0f, c.u (2.0f)));
    c.line (cabinetLeft, 39.0f, cabinetLeft + cabinetWidth, 39.0f, 1.25f);

    // Twin reel hubs and a tape head give the upper bay a transport-machine
    // reading.  Rotating index marks make the motion mechanical and steady.
    constexpr float reelY = 28.5f;
    constexpr float reelX[] = { 23.0f, 38.0f };
    for (int i = 0; i < 2; ++i)
    {
        c.colour (c.p.ink);
        c.g.drawEllipse (c.rect (reelX[i] - 5.0f, reelY - 5.0f, 10.0f, 10.0f),
                         juce::jmax (1.0f, c.u (1.5f)));
        c.dot (reelX[i], reelY, 1.5f, c.p.ink);

        if (! compact)
        {
            const float reelAngle = cycle * juce::MathConstants<float>::twoPi
                                  * (i == 0 ? 1.0f : -1.0f);
            c.line (reelX[i], reelY,
                    reelX[i] + std::cos (reelAngle) * 3.5f,
                    reelY + std::sin (reelAngle) * 3.5f, 1.0f);
        }
    }

    if (! compact)
    {
        juce::Path tape;
        tape.startNewSubPath (c.pt (18.0f, reelY));
        tape.cubicTo (c.pt (18.0f, 20.5f), c.pt (43.0f, 20.5f), c.pt (43.0f, reelY));
        tape.cubicTo (c.pt (43.0f, 35.0f), c.pt (32.0f, 36.5f), c.pt (29.0f, 34.0f));
        c.colour (c.p.mutedInk, 0.9f);
        c.stroke (tape, 1.0f);
        c.filledRect (28.0f, 33.0f, 4.0f, 3.0f, c.p.vermilion);
    }

    // The large woofer supplies the sound-system weight.  Its inner pressure
    // ring breathes with the send, with wetness controlling excursion.
    const float wooferRadius = 11.5f + bassPulse * (1.0f + wet * 1.8f);
    c.colour (c.p.ink);
    c.g.drawEllipse (c.rect (30.5f - wooferRadius, wooferY - wooferRadius,
                             wooferRadius * 2.0f, wooferRadius * 2.0f),
                     juce::jmax (1.0f, c.u (2.0f)));
    c.g.drawEllipse (c.rect (25.5f, wooferY - 5.0f, 10.0f, 10.0f),
                     juce::jmax (1.0f, c.u (1.5f)));
    c.dot (30.5f, wooferY, 2.2f + bassPulse * wet, c.p.vermilion);
    c.colour (c.p.ink);
    c.line (19.0f, 76.5f, 42.0f, 76.5f, 1.5f);

    // Three delayed, slightly irregular orbital wavefronts bloom from the
    // cabinet.  Feedback lengthens their persistence; the coloured lamps ride
    // the fronts as redundant reggae/dub signal coding.
    const juce::Colour echoColours[] = { c.p.vermilion, c.p.safetyYellow, c.p.signalGreen };
    const int echoCount = compact ? 2 : 3;
    for (int echo = echoCount - 1; echo >= 0; --echo)
    {
        const float echoPhase = wrapped (cycle - static_cast<float> (echo) / echoCount);
        const float radius = 9.0f + echoPhase * 34.0f;
        const float radiusX = radius;
        const float radiusY = radius * (0.70f + 0.06f * echo);
        const float lifecycle = std::pow (
            std::sin (echoPhase * juce::MathConstants<float>::pi), 0.65f);
        const float persistence = lifecycle
                                * juce::jlimit (0.0f, 0.92f,
                                               (1.0f - echoPhase)
                                               * (0.55f + feedback * 0.62f));
        juce::Path bloom;
        constexpr int points = 34;
        for (int i = 0; i <= points; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (points);
            const float angle = -juce::MathConstants<float>::halfPi
                              + t * juce::MathConstants<float>::pi;
            const float ripple = std::sin (
                angle * 3.0f + echo * 1.7f
                + echoPhase * juce::MathConstants<float>::twoPi) * 0.8f;
            const auto point = c.pt (sourceX + std::cos (angle) * (radiusX + ripple),
                                     waveY + std::sin (angle) * (radiusY + ripple * 0.4f));
            if (i == 0) bloom.startNewSubPath (point); else bloom.lineTo (point);
        }
        c.colour (c.p.ink, persistence);
        c.stroke (bloom, echo == 0 ? 2.0f : 1.5f);

        const float lampX = sourceX + radiusX;
        c.dot (lampX, waveY, compact ? 2.4f : 2.8f, echoColours[echo],
               lifecycle * (0.58f + wet * 0.42f));
    }
}

void drawReverb (GlyphCanvas& c, float phase)
{
    const int contourCount = c.compact ? 3 : 4;
    constexpr float centreX = 48.0f;
    constexpr float centreY = 51.0f;
    constexpr float start = 0.34f;
    constexpr float end = juce::MathConstants<float>::twoPi - 0.42f;
    for (int i = contourCount - 1; i >= 0; --i)
    {
        const float radius = 11.0f + i * (c.compact ? 10.0f : 9.0f);
        c.colour (i == 0 ? c.p.ink : c.p.mutedInk, 0.96f - i * 0.12f);
        c.stroke (irregularArc (c, centreX, centreY, radius,
                                0.55f + i * 0.28f, phase + i * 0.13f,
                                start, end),
                  i == 0 ? 2.2f : 1.55f);
    }

    c.ring (centreX, centreY, 5.0f, c.p.vermilion, 2.0f);
    c.dot (centreX, centreY, 2.2f, c.p.vermilion);
    c.colour (c.p.vermilion);
    c.line (79.0f, 39.0f, 86.0f, 36.0f, 2.0f);
    c.terminal (86.0f, 36.0f, -0.38f, c.p.ink, 6.0f, 1.2f);
}

void drawFilter (GlyphCanvas& c, float phase, bool highPass, bool sampleHold, bool master)
{
    if (master)
    {
        c.colour (c.p.ink);
        c.line (9.0f, 20.0f, 9.0f, 80.0f, 2.5f);
        c.line (91.0f, 20.0f, 91.0f, 80.0f, 2.5f);
        c.line (9.0f, 20.0f, 21.0f, 20.0f, 2.5f);
        c.line (79.0f, 20.0f, 91.0f, 20.0f, 2.5f);
        c.line (9.0f, 80.0f, 21.0f, 80.0f, 2.5f);
        c.line (79.0f, 80.0f, 91.0f, 80.0f, 2.5f);
    }

    if (sampleHold)
    {
        constexpr float fullPosts[] = { 12.0f, 23.0f, 36.0f, 49.0f, 63.0f, 75.0f };
        constexpr float compactPosts[] = { 15.0f, 30.0f, 46.0f, 62.0f, 75.0f };
        const float* posts = c.compact ? compactPosts : fullPosts;
        const int numPosts = c.compact ? static_cast<int> (std::size (compactPosts))
                                       : static_cast<int> (std::size (fullPosts));
        const int heldFrame = static_cast<int> (
            wrapped (phase) * static_cast<float> (numPosts)) % numPosts;
        float values[6] {};
        for (int i = 0; i < numPosts; ++i)
        {
            const int hash = (i * 31 + i * i * 9 + heldFrame * 23 + 17) % 45;
            values[i] = 29.0f + static_cast<float> (hash);
        }

        juce::Path holds;
        holds.startNewSubPath (c.pt (posts[0], values[0]));
        for (int i = 0; i < numPosts - 1; ++i)
        {
            holds.lineTo (c.pt (posts[i + 1], values[i]));
            holds.lineTo (c.pt (posts[i + 1], values[i + 1]));
        }
        c.colour (c.p.ink);
        c.machineStroke (holds, 2.2f);

        if (! c.compact)
        {
            const int activeSample = heldFrame % numPosts;
            for (int i = 0; i < numPosts; ++i)
                c.filledRect (posts[i] - 1.6f, values[i] - 1.6f, 3.2f, 3.2f,
                              i == activeSample ? c.p.vermilion : c.p.ink,
                              i == activeSample ? 1.0f : 0.72f);
        }

        // A short mirrored filter fin acts as a family badge.  It avoids the
        // visual language of a prohibition slash through the held signal.
        c.colour (c.p.vermilion);
        c.line (77.0f, highPass ? 68.0f : 32.0f,
                89.0f, highPass ? 45.0f : 55.0f, 2.8f);
        c.terminal (89.0f, highPass ? 45.0f : 55.0f,
                    highPass ? -1.09f : 1.09f, c.p.ink, 6.0f, 1.3f);
        return;
    }

    const int stepCount = c.compact ? 3 : 5;
    const float left = master ? 22.0f : 17.0f;
    const float stepWidth = (master ? 56.0f : 66.0f) / static_cast<float> (stepCount);
    const float stepHeight = c.compact ? 13.0f : 9.0f;
    const float direction = highPass ? -1.0f : 1.0f;
    float y = highPass ? 72.0f : 28.0f;
    for (int i = 0; i < stepCount; ++i)
    {
        const bool passed = highPass ? i >= stepCount / 2 : i <= stepCount / 2;
        c.colour (passed ? c.p.ink : c.p.mutedInk, passed ? 1.0f : 0.65f);
        const float x0 = left + i * stepWidth;
        c.line (x0, y, x0 + stepWidth, y, passed ? 2.2f : 1.5f);
        c.line (x0 + stepWidth, y, x0 + stepWidth, y + direction * stepHeight,
                passed ? 2.0f : 1.35f);
        y += direction * stepHeight;
    }

    const float sweep = (triangle (phase) - 0.5f) * 5.0f;
    c.colour (c.p.vermilion);
    c.line (32.0f, highPass ? 70.0f - sweep : 30.0f + sweep,
            69.0f, highPass ? 33.0f + sweep : 67.0f - sweep, 2.7f);
    c.dot (50.5f, 51.5f, c.compact ? 2.2f : 2.8f, c.p.vermilion);

    if (master)
    {
        c.colour (c.p.mutedInk);
        for (int i = 0; i < 8; ++i)
            c.line (18.5f + i * 9.0f, 87.0f, 18.5f + i * 9.0f, 92.0f,
                    i == 0 || i == 7 ? 2.0f : 1.25f);
    }
}

void drawVolumeRamp (GlyphCanvas& c, float phase)
{
    const float voiceLevel = 1.0f - wrapped (phase);

    if (c.compact)
    {
        // Preserve the odd shushing character at queue size: simplified
        // anatomy, one unmistakable finger, and a fading voice trail.
        c.ring (34.0f, 42.0f, 14.0f, c.p.ink, 2.4f);
        c.dot (21.0f, 29.0f, 5.0f, c.p.ink);
        c.colour (c.p.ink);
        c.line (25.0f, 39.0f, 42.0f, 39.0f, 2.0f);

        juce::Path body;
        body.startNewSubPath (c.pt (14.0f, 83.0f));
        body.lineTo (c.pt (21.0f, 65.0f));
        body.lineTo (c.pt (34.0f, 58.0f));
        body.lineTo (c.pt (47.0f, 65.0f));
        body.lineTo (c.pt (53.0f, 83.0f));
        c.stroke (body, 2.3f);

        c.colour (c.p.vermilion);
        c.line (45.0f, 65.0f, 45.0f, 35.0f, 3.2f);
        c.dot (45.0f, 33.0f, 2.2f, c.p.vermilion);

        constexpr float radii[] = { 3.8f, 2.8f, 1.8f };
        for (int i = 0; i < 3; ++i)
            c.ring (58.0f + i * 11.0f, 45.0f + i * 2.0f,
                    radii[i], c.p.safetyYellow, 1.6f,
                    0.45f + voiceLevel * 0.55f);
        return;
    }

    // Two deliberately geometric figures keep the strange shushing metaphor
    // while avoiding miniature portrait detail.
    c.colour (c.p.ink);
    c.ring (31.0f, 38.0f, 14.0f, c.p.ink, 2.2f);
    c.dot (18.5f, 25.0f, 5.5f, c.p.ink);
    c.ring (27.0f, 36.5f, 4.0f, c.p.ink, 1.4f);
    c.ring (36.0f, 36.5f, 4.0f, c.p.ink, 1.4f);
    c.line (31.0f, 36.5f, 32.0f, 36.5f, 1.1f);
    c.dot (27.0f, 36.5f, 0.9f, c.p.ink);
    c.dot (36.0f, 36.5f, 0.9f, c.p.ink);
    c.line (36.0f, 47.0f, 41.0f, 47.0f, 1.4f);

    juce::Path librarianBody;
    librarianBody.startNewSubPath (c.pt (10.0f, 83.0f));
    librarianBody.lineTo (c.pt (17.0f, 62.0f));
    librarianBody.lineTo (c.pt (30.0f, 57.0f));
    librarianBody.lineTo (c.pt (44.0f, 63.0f));
    librarianBody.lineTo (c.pt (50.0f, 83.0f));
    c.stroke (librarianBody, 2.2f);

    juce::Path shushingArm;
    shushingArm.startNewSubPath (c.pt (28.0f, 72.0f));
    shushingArm.lineTo (c.pt (43.0f, 59.0f));
    shushingArm.lineTo (c.pt (43.0f, 37.0f));
    c.colour (c.p.vermilion);
    c.stroke (shushingArm, 3.0f);
    c.dot (43.0f, 36.0f, 2.0f, c.p.vermilion);

    c.colour (c.p.ink);
    c.ring (76.0f, 43.0f, 10.5f, c.p.ink, 2.1f);
    c.line (66.0f, 43.0f, 61.5f, 46.0f, 1.6f);
    c.line (61.5f, 46.0f, 66.0f, 49.0f, 1.6f);
    c.dot (72.0f, 40.0f, 1.0f, c.p.ink);

    juce::Path talkerBody;
    talkerBody.startNewSubPath (c.pt (58.0f, 83.0f));
    talkerBody.lineTo (c.pt (64.0f, 65.0f));
    talkerBody.lineTo (c.pt (77.0f, 58.0f));
    talkerBody.lineTo (c.pt (90.0f, 66.0f));
    talkerBody.lineTo (c.pt (94.0f, 83.0f));
    c.stroke (talkerBody, 2.2f);

    c.colour (c.p.safetyYellow, 0.20f + voiceLevel * 0.80f);
    for (int i = 0; i < 3; ++i)
    {
        const float radius = (3.4f - i * 0.75f) * voiceLevel;
        if (radius > 0.35f)
            c.ring (56.0f - i * 6.0f, 43.0f + i * 3.0f,
                    radius, c.p.safetyYellow, 1.5f, 0.3f + voiceLevel * 0.7f);
    }
}

void drawTremolo (GlyphCanvas& c, float phase)
{
    c.colour (c.p.mutedInk);
    c.line (17.0f, 50.0f, 89.0f, 50.0f, 1.0f);

    juce::Path modulated;
    constexpr int points = 72;
    const float depth = 0.55f + triangle (phase) * 0.45f;
    for (int i = 0; i <= points; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (points);
        const float halfWave = std::floor (t * 7.0f);
        const float alternating = static_cast<int> (halfWave) % 2 == 0 ? 1.0f : 0.52f;
        const float y = 50.0f + std::sin (t * 7.0f * juce::MathConstants<float>::pi)
                                  * 21.0f * depth * alternating;
        if (i == 0) modulated.startNewSubPath (c.pt (18.0f, y));
        else        modulated.lineTo (c.pt (18.0f + t * 70.0f, y));
    }
    c.colour (c.p.ink);
    c.stroke (modulated, 2.5f);

    c.colour (c.p.ink);
    c.line (12.0f, 29.0f, 12.0f, 71.0f, 1.7f);
    c.line (9.0f, 29.0f, 15.0f, 29.0f, 1.3f);
    c.line (9.0f, 50.0f, 15.0f, 50.0f, 1.3f);
    c.line (9.0f, 71.0f, 15.0f, 71.0f, 1.3f);
    c.dot (12.0f, 50.0f - depth * 19.0f, c.compact ? 2.3f : 2.8f, c.p.vermilion);
}

void drawChorus (GlyphCanvas& c, float phase, const ModifierDescriptor& descriptor)
{
    const int voiceCount = c.compact ? 2 : 3;
    const float depth = static_cast<float> (juce::jlimit (0.0, 1.0,
                                          descriptor.plannedChorusDepth.value_or (0.7)));
    const float mix = static_cast<float> (juce::jlimit (0.0, 1.0,
                                        descriptor.plannedChorusMix.value_or (0.5)));
    const float rate = static_cast<float> (juce::jlimit (0.2, 2.0,
                                         descriptor.plannedChorusRateHz.value_or (0.8) / 0.8));
    for (int voice = 0; voice < voiceCount; ++voice)
    {
        const float voicePosition = c.compact ? (voice == 0 ? 0.0f : 1.0f)
                                              : static_cast<float> (voice - 1);
        juce::Path trace;
        constexpr int points = 56;
        for (int i = 0; i <= points; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (points);
            const float fan = std::sin (t * juce::MathConstants<float>::pi)
                            * (4.0f + depth * 7.0f);
            const float carrier = std::sin (t * 2.0f * juce::MathConstants<float>::twoPi) * 5.0f;
            const float phaseDrift = voicePosition
                                   * std::sin (t * juce::MathConstants<float>::pi)
                                   * std::sin (phase * juce::MathConstants<float>::twoPi)
                                   * (1.2f + depth * 2.2f)
                                   * (0.65f + rate * 0.35f);
            const float y = 50.0f + carrier + voicePosition * fan + phaseDrift;
            if (i == 0) trace.startNewSubPath (c.pt (13.0f, y));
            else        trace.lineTo (c.pt (13.0f + t * 74.0f, y));
        }

        const auto colour = voicePosition < 0.0f ? c.p.ultramarine
                           : (voicePosition > 0.0f ? c.p.vermilion : c.p.ink);
        c.colour (colour, voicePosition == 0.0f ? 1.0f : 0.52f + mix * 0.46f);
        c.stroke (trace, voicePosition == 0.0f ? 2.2f : 2.0f);
    }
    c.dot (13.0f, 50.0f, 2.1f, c.p.ink);
    c.dot (87.0f, 50.0f, 2.1f, c.p.ink);
}

void drawAutoPan (GlyphCanvas& c, float phase, const ModifierDescriptor& descriptor)
{
    juce::Path orbit;
    orbit.startNewSubPath (c.pt (16.0f, 50.0f));
    orbit.cubicTo (c.pt (29.0f, 24.0f), c.pt (71.0f, 24.0f), c.pt (84.0f, 50.0f));
    orbit.cubicTo (c.pt (71.0f, 76.0f), c.pt (29.0f, 76.0f), c.pt (16.0f, 50.0f));
    c.colour (c.p.ink);
    c.stroke (orbit, 2.2f);
    c.line (12.0f, 30.0f, 12.0f, 70.0f, 2.3f);
    c.line (88.0f, 30.0f, 88.0f, 70.0f, 2.3f);
    c.line (12.0f, 36.0f, 17.0f, 41.0f, 1.5f);
    c.line (12.0f, 64.0f, 17.0f, 59.0f, 1.5f);
    c.line (88.0f, 36.0f, 83.0f, 41.0f, 1.5f);
    c.line (88.0f, 64.0f, 83.0f, 59.0f, 1.5f);
    c.colour (c.p.mutedInk);
    c.line (50.0f, 40.0f, 50.0f, 60.0f, 1.0f);
    const float depth = static_cast<float> (juce::jlimit (0.0, 1.0, descriptor.plannedPanDepth.value_or (1.0)));
    const float mix = static_cast<float> (juce::jlimit (0.0, 1.0, descriptor.plannedPanMix.value_or (0.75)));
    const float rate = static_cast<float> (juce::jlimit (0.25, 2.0,
                                         descriptor.plannedPanRateHz.value_or (0.5) / 0.5));
    const float travel = 64.0f * depth;
    const float shapedTraversal = std::pow (triangle (phase), 1.0f / rate);
    const float signalX = 50.0f - travel * 0.5f + shapedTraversal * travel;
    c.ring (signalX, 50.0f, c.compact ? 4.5f : 5.3f, c.p.ink, 1.2f, 0.75f);
    c.dot (signalX, 50.0f, c.compact ? 3.0f : 3.5f,
           c.p.vermilion, 0.58f + mix * 0.42f);
}

void drawDucking (GlyphCanvas& c, float phase)
{
    const float pulseX = 24.0f + wrapped (phase) * 52.0f;
    juce::Path ducked;
    constexpr int points = 72;
    for (int i = 0; i <= points; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (points);
        const float x = 12.0f + t * 76.0f;
        const float carrier = std::sin (t * 7.0f * juce::MathConstants<float>::twoPi) * 5.0f;
        const float distance = (x - pulseX) / 10.0f;
        const float depression = std::exp (-distance * distance) * 18.0f;
        const float y = 59.0f + carrier + depression;
        if (i == 0) ducked.startNewSubPath (c.pt (x, y));
        else        ducked.lineTo (c.pt (x, y));
    }

    c.colour (c.p.mutedInk);
    c.line (12.0f, 59.0f, 88.0f, 59.0f, 1.0f);
    c.colour (c.p.ink);
    c.stroke (ducked, 2.2f);

    c.colour (c.p.vermilion);
    c.filledRect (pulseX - 6.0f, 17.0f, 12.0f, 6.0f, c.p.vermilion);
    c.line (pulseX, 23.0f, pulseX, 44.0f, 3.8f);
    c.arrowHead (pulseX, 49.0f, juce::MathConstants<float>::halfPi, 5.0f);
    c.line (pulseX - 7.0f, 64.0f, pulseX, 76.0f, 2.5f);
    c.line (pulseX, 76.0f, pulseX + 7.0f, 64.0f, 2.5f);
}

void drawGranular (GlyphCanvas& c, float phase, bool momentary, const ModifierDescriptor& d)
{
    int count = momentary ? 8 : 9;
    if (d.plannedGrainDensityHz.has_value())
        count = juce::jlimit (6, momentary ? 9 : 10,
                             5 + static_cast<int> (d.plannedGrainDensityHz.value() * 0.25));
    count = juce::jmin (count, c.compact ? 6 : 9);

    const float sizeScale = static_cast<float> (juce::jlimit (0.75, 1.35,
        d.plannedGrainSizeMs.value_or (160.0) / 160.0));

    if (momentary)
    {
        constexpr float plume[][2] = {
            { 0.25f, -0.85f }, { 0.55f, -0.62f }, { 0.82f, -0.35f },
            { 0.96f, -0.05f }, { 0.70f,  0.23f }, { 0.42f,  0.48f },
            { 0.08f, -0.48f }, { 0.62f, -0.92f }, { 0.98f,  0.36f }
        };
        const float burst = c.compact ? juce::jmax (0.72f, triangle (phase))
                                      : triangle (phase);
        const float seedX = c.compact ? 24.0f : 28.0f;
        const float seedY = c.compact ? 64.0f : 63.0f;

        c.colour (c.p.mutedInk);
        c.stroke (wavePath (c, c.compact ? 10.0f : 15.0f, c.compact ? 43.0f : 45.0f,
                            seedY, c.compact ? 4.8f : 4.0f, 1.4f), 1.6f);
        c.ring (seedX, seedY, c.compact ? 6.0f : 5.0f, c.p.vermilion, 2.0f);
        c.dot (seedX, seedY, c.compact ? 2.6f : 2.1f, c.p.vermilion);

        for (int i = 0; i < count; ++i)
        {
            const float distance = (c.compact ? 17.0f + i * 5.0f
                                              : 13.0f + i * 4.0f) * burst;
            const float px = seedX + plume[i][0] * distance;
            const float py = seedY + plume[i][1] * distance;
            const float opacity = 0.38f + (1.0f - burst) * 0.52f;
            c.dot (px, py, (1.35f + (i % 3) * 0.45f) * sizeScale
                           * (c.compact ? 1.15f : 1.0f),
                   i == 2 || i == 7 ? c.p.vermilion : c.p.ink, opacity);
        }

        if (! c.compact)
        {
            c.colour (c.p.vermilion, 0.55f + burst * 0.35f);
            c.line (34.0f, 49.0f, 38.0f + burst * 5.0f, 42.0f - burst * 3.0f, 1.4f);
            c.line (43.0f, 57.0f, 50.0f + burst * 5.0f, 55.0f - burst * 2.0f, 1.4f);
        }
        return;
    }

    constexpr float cloud[][2] = {
        { -27.0f, -8.0f }, { -19.0f, 14.0f }, { -11.0f, -20.0f },
        {  -3.0f, 17.0f }, {   7.0f, -12.0f }, {  16.0f, 10.0f },
        {  27.0f, -5.0f }, {  13.0f, 24.0f }, { -25.0f, 23.0f }
    };
    c.colour (c.p.mutedInk);
    c.stroke (wavePath (c, 22.0f, 78.0f, 52.0f, 5.0f, 2.0f), 1.7f);

    for (int i = 0; i < count; ++i)
    {
        const float driftX = std::sin (phase * juce::MathConstants<float>::twoPi + i * 1.7f) * 1.8f;
        const float driftY = std::cos (phase * juce::MathConstants<float>::twoPi + i * 1.1f) * 1.4f;
        c.dot (50.0f + cloud[i][0] + driftX, 49.0f + cloud[i][1] + driftY,
               (1.4f + (i % 3) * 0.45f) * sizeScale,
               i == 2 || i == 6 ? c.p.vermilion : c.p.ink);
    }

    c.colour (c.p.ink);
    c.line (15.0f, 27.0f, 15.0f, 36.0f, 1.5f);
    c.line (15.0f, 27.0f, 24.0f, 27.0f, 1.5f);
    c.line (85.0f, 66.0f, 85.0f, 75.0f, 1.5f);
    c.line (76.0f, 75.0f, 85.0f, 75.0f, 1.5f);
}

void drawSwitchPart (GlyphCanvas& c, float phase, const ModifierDescriptor& descriptor)
{
    const float shift = triangle (phase) * 6.0f;
    const float frontX = 20.0f + shift;
    constexpr float frontY = 31.0f;
    constexpr float cardWidth = 44.0f;
    constexpr float cardHeight = 50.0f;

    juce::Path backCard;
    backCard.startNewSubPath (c.pt (34.0f, 22.0f));
    backCard.lineTo (c.pt (70.0f, 22.0f));
    backCard.lineTo (c.pt (77.0f, 29.0f));
    backCard.lineTo (c.pt (77.0f, 72.0f));
    backCard.lineTo (c.pt (34.0f, 72.0f));
    backCard.closeSubPath();
    c.colour (c.p.mutedInk);
    c.machineStroke (backCard, 1.6f);

    juce::Path frontCard;
    frontCard.startNewSubPath (c.pt (frontX, frontY));
    frontCard.lineTo (c.pt (frontX + cardWidth - 8.0f, frontY));
    frontCard.lineTo (c.pt (frontX + cardWidth, frontY + 8.0f));
    frontCard.lineTo (c.pt (frontX + cardWidth, frontY + cardHeight));
    frontCard.lineTo (c.pt (frontX, frontY + cardHeight));
    frontCard.closeSubPath();
    c.colour (c.p.ink);
    c.g.fillPath (frontCard);

    c.filledRect (frontX + cardWidth - 4.0f, frontY + 19.0f,
                  7.0f, 12.0f, c.p.vermilion);
    c.colour (c.p.raisedTile);
    c.g.setFont (juce::Font (juce::FontOptions().withHeight (c.u (c.compact ? 19.0f : 22.0f))).boldened());
    const auto destination = descriptor.plannedDestinationPart.value_or (1);
    const auto partLabel = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + juce::jlimit (0, 25, destination)));
    c.g.drawText (partLabel, c.rect (frontX, frontY, cardWidth, cardHeight), juce::Justification::centred);

    c.colour (c.p.vermilion);
    juce::Path transfer;
    transfer.startNewSubPath (c.pt (84.0f, 61.0f));
    transfer.lineTo (c.pt (84.0f, 86.0f));
    transfer.lineTo (c.pt (frontX + 33.0f, 86.0f));
    c.machineStroke (transfer, 2.0f);
    if (! c.compact)
        c.arrowHead (frontX + 28.0f, 86.0f, juce::MathConstants<float>::pi, 4.3f);
}

void drawQuarterBurst (GlyphCanvas& c, float phase)
{
    c.colour (c.p.ink);
    c.line (13.0f, 59.0f, 87.0f, 59.0f, 2.0f);
    c.terminal (13.0f, 59.0f, 0.0f, c.p.ink, 8.0f, 1.6f);
    c.terminal (87.0f, 59.0f, 0.0f, c.p.ink, 8.0f, 1.6f);
    const int active = static_cast<int> (wrapped (phase) * 4.0f) % 4;
    for (int i = 0; i < 4; ++i)
    {
        const float px = 21.0f + i * 19.3f;
        c.dot (px, 59.0f, i == active ? 4.6f : 2.8f,
               i == active ? c.p.vermilion : c.p.ink);
        c.colour (i == active ? c.p.vermilion : c.p.mutedInk);
        c.line (px, i == active ? 27.0f : 33.0f, px, 48.0f,
                i == active ? 3.0f : 1.5f);
        if (i == active && ! c.compact)
        {
            c.line (px - 6.0f, 31.0f, px - 3.0f, 35.0f, 1.4f);
            c.line (px + 6.0f, 31.0f, px + 3.0f, 35.0f, 1.4f);
        }
    }
}

void drawSwap (GlyphCanvas& c, float phase)
{
    const float shift = triangle (phase) * 5.0f;
    constexpr float stackWidth = 34.0f;
    constexpr float layerInset = 5.0f;
    constexpr float layerWidth = stackWidth - layerInset * 2.0f;

    auto cartridge = [&c] (float x)
    {
        juce::Path path;
        path.startNewSubPath (c.pt (x + 5.0f, 21.0f));
        path.lineTo (c.pt (x + 29.0f, 21.0f));
        path.lineTo (c.pt (x + 34.0f, 26.0f));
        path.lineTo (c.pt (x + 34.0f, 76.0f));
        path.lineTo (c.pt (x, 76.0f));
        path.lineTo (c.pt (x, 26.0f));
        path.closeSubPath();
        return path;
    };

    c.colour (c.p.ink);
    c.machineStroke (cartridge (5.0f), 2.1f);
    c.machineStroke (cartridge (61.0f), 2.1f);

    const int layerCount = c.compact ? 2 : 3;
    for (int i = 0; i < layerCount; ++i)
    {
        const auto colour = i == 0 ? c.p.vermilion
                          : (i == 1 ? c.p.ultramarine : c.p.safetyYellow);
        const float y = c.compact ? 35.0f + i * 22.0f : 31.0f + i * 14.0f;
        c.filledRect (5.0f + layerInset + shift * (i == 0 ? 1.0f : 0.0f),
                      y, layerWidth, 6.0f, colour);
        c.filledRect (61.0f + layerInset - shift * (i == layerCount - 1 ? 1.0f : 0.0f),
                      y, layerWidth, 6.0f, colour);
    }

    // The routes stop at a central switching socket, creating a deliberate
    // crossing gap instead of a fragile hairline X.
    c.colour (c.p.ink);
    c.line (40.0f, 28.0f, 47.0f, 46.0f, 2.2f);
    c.line (53.0f, 54.0f, 60.0f, 72.0f, 2.2f);
    c.line (40.0f, 72.0f, 47.0f, 54.0f, 2.2f);
    c.line (53.0f, 46.0f, 60.0f, 28.0f, 2.2f);
    c.ring (50.0f, 50.0f, 4.2f, c.p.ink, 1.8f);
    if (! c.compact)
    {
        c.colour (c.p.ink);
        c.arrowHead (61.5f, 74.5f, 1.18f, 3.8f);
        c.arrowHead (61.5f, 25.5f, -1.18f, 3.8f);
    }
}

void drawReset (GlyphCanvas& c, float phase)
{
    const float retract = 1.0f - triangle (phase) * 0.28f;
    const juce::Colour colours[] = { c.p.vermilion, c.p.safetyYellow, c.p.signalGreen, c.p.ultramarine };
    constexpr float lengths[] = { 39.0f, 34.0f, 42.0f, 36.0f };
    for (int i = 0; i < 4; ++i)
    {
        const float a = juce::MathConstants<float>::halfPi * i
                      + juce::MathConstants<float>::pi * 0.25f;
        const float sx = 50.0f + std::cos (a) * lengths[i] * retract;
        const float sy = 50.0f + std::sin (a) * lengths[i] * retract;
        const float ex = 50.0f + std::cos (a) * 9.0f;
        const float ey = 50.0f + std::sin (a) * 9.0f;
        c.colour (colours[i]);
        c.line (sx, sy, ex, ey, 2.6f);
        c.terminal (sx, sy, a, colours[i], c.compact ? 5.0f : 7.0f, 1.5f);
        if (! c.compact)
            c.arrowHead (ex, ey, a + juce::MathConstants<float>::pi, 4.2f);
    }
    c.ring (50.0f, 50.0f, 7.0f, c.p.ink, 2.2f);
    c.dot (50.0f, 50.0f, 1.8f + triangle (phase) * 2.7f, c.p.ink);
}

void drawUnknown (GlyphCanvas& c)
{
    c.colour (c.p.mutedInk);
    c.g.drawEllipse (c.rect (25.0f, 25.0f, 50.0f, 50.0f), c.u (2.0f));
    c.line (34.0f, 50.0f, 66.0f, 50.0f, 2.0f);
    c.line (50.0f, 34.0f, 50.0f, 66.0f, 2.0f);
}
} // namespace

void ModifierGlyphRenderer::draw (juce::Graphics& g,
                                  juce::Rectangle<float> bounds,
                                  const ModifierGlyphState& state,
                                  const ControlSurfacePalette& palette)
{
    if (bounds.isEmpty()) return;

    const float phase = state.reducedMotion
                            ? ModifierRegistry::get (state.descriptor.type).representativeGlyphPhase01
                            : wrapped (state.phase01);
    GlyphCanvas c (g, bounds, palette, state.emphasis01, state.compact);

    switch (state.descriptor.type)
    {
        case ModifierType::Reverse:                    drawReverse (c, phase); break;
        case ModifierType::Speed:                      drawSpeed (c, phase, state.descriptor); break;
        case ModifierType::Stretch:                    drawStretch (c, phase, state.descriptor); break;
        case ModifierType::PitchUpOctave:              drawPitch (c, phase, true); break;
        case ModifierType::PitchDownOctave:            drawPitch (c, phase, false); break;
        case ModifierType::BeatSliceRandom:
        case ModifierType::SliceRepeater:               drawSliceBlocks (c, phase, state.descriptor.type, state.descriptor); break;
        case ModifierType::ArpSlice:                     drawArpSlice (c, phase, state.descriptor); break;
        case ModifierType::PingPong:                    drawPingPong (c, phase); break;
        case ModifierType::BufferDelayOn:               drawDelay (c, phase, state.descriptor); break;
        case ModifierType::BufferDelayDubBurst:         drawDubDelay (c, phase, state.descriptor, state.compact); break;
        case ModifierType::BufferReverbOn:              drawReverb (c, phase); break;
        case ModifierType::BufferLowPassOn:             drawFilter (c, phase, false, false, false); break;
        case ModifierType::BufferHighPassOn:            drawFilter (c, phase, true, false, false); break;
        case ModifierType::BufferVolumeRampDown:        drawVolumeRamp (c, phase); break;
        case ModifierType::BufferTremolo:               drawTremolo (c, phase); break;
        case ModifierType::BufferChorusOn:              drawChorus (c, phase, state.descriptor); break;
        case ModifierType::BufferAutoPan:               drawAutoPan (c, phase, state.descriptor); break;
        case ModifierType::BufferDuckingOn:             drawDucking (c, phase); break;
        case ModifierType::BufferSHLowPassOn:           drawFilter (c, phase, false, true, false); break;
        case ModifierType::BufferSHHighPassOn:          drawFilter (c, phase, true, true, false); break;
        case ModifierType::BufferGranularOn:            drawGranular (c, phase, false, state.descriptor); break;
        case ModifierType::BufferGranularMomentary:     drawGranular (c, phase, true, state.descriptor); break;
        case ModifierType::MasterHighPassOn:            drawFilter (c, phase, true, false, true); break;
        case ModifierType::MasterLowPassOn:             drawFilter (c, phase, false, false, true); break;
        case ModifierType::SwitchPart:                  drawSwitchPart (c, phase, state.descriptor); break;
        case ModifierType::QuarterNoteBurst:            drawQuarterBurst (c, phase); break;
        case ModifierType::SwapModifierStack:           drawSwap (c, phase); break;
        case ModifierType::ResetAll:                    drawReset (c, phase); break;
        case ModifierType::Unknown:                     drawUnknown (c); break;
    }
}
