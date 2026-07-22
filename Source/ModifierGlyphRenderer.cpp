#include "ModifierGlyphRenderer.h"

#include <cmath>

namespace
{
struct GlyphCanvas
{
    GlyphCanvas (juce::Graphics& graphics, juce::Rectangle<float> target,
                 const ControlSurfacePalette& colours, float opacity)
        : g (graphics), p (colours), alpha (juce::jlimit (0.0f, 1.0f, opacity))
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
        g.drawLine ({ x (x1), y (y1), x (x2), y (y2) }, juce::jmax (1.0f, u (width)));
    }

    void stroke (const juce::Path& path, float width = 2.0f)
    {
        g.strokePath (path, juce::PathStrokeType (juce::jmax (1.0f, u (width)),
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    void dot (float px, float py, float radius, juce::Colour c)
    {
        colour (c);
        g.fillEllipse (rect (px - radius, py - radius, radius * 2.0f, radius * 2.0f));
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

juce::Path irregularRing (const GlyphCanvas& c, float cx, float cy, float radius,
                          float wobble, float phase)
{
    juce::Path path;
    constexpr int points = 72;
    for (int i = 0; i <= points; ++i)
    {
        const float a = static_cast<float> (i) / static_cast<float> (points)
                      * juce::MathConstants<float>::twoPi;
        const float r = radius + std::sin (a * 3.0f + phase * juce::MathConstants<float>::twoPi) * wobble;
        const auto point = c.pt (cx + std::cos (a) * r, cy + std::sin (a) * r);
        if (i == 0) path.startNewSubPath (point); else path.lineTo (point);
    }
    path.closeSubPath();
    return path;
}

void drawReverse (GlyphCanvas& c, float phase)
{
    c.colour (c.p.ink);
    const float shift = (wrapped (phase) - 0.5f) * 8.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float px = 32.0f + i * 18.0f + shift;
        juce::Path chevron;
        chevron.startNewSubPath (c.pt (px + 10.0f, 32.0f));
        chevron.lineTo (c.pt (px, 50.0f));
        chevron.lineTo (c.pt (px + 10.0f, 68.0f));
        c.stroke (chevron, 3.0f);
    }
    c.colour (c.p.vermilion);
    c.line (22.0f, 77.0f, 78.0f, 77.0f, 2.0f);
}

void drawSpeed (GlyphCanvas& c, float phase)
{
    c.colour (c.p.ink);
    const float breathe = triangle (phase) * 5.0f;
    const float positions[] = { 18.0f, 30.0f + breathe, 45.0f + breathe * 0.5f, 64.0f, 82.0f };
    for (int i = 0; i < 5; ++i)
        c.line (positions[i], i % 2 == 0 ? 28.0f : 35.0f, positions[i], i % 2 == 0 ? 72.0f : 65.0f, 2.0f);
    c.colour (c.p.vermilion);
    c.line (20.0f, 80.0f, 78.0f, 80.0f, 2.0f);
    c.arrowHead (82.0f, 80.0f, 0.0f, 6.0f);
}

void drawStretch (GlyphCanvas& c, float phase)
{
    const float spread = 4.0f * triangle (phase);
    const float left = 20.0f - spread;
    const float right = 80.0f + spread;
    c.colour (c.p.ink);
    c.line (left, 22.0f, left, 78.0f, 3.0f);
    c.line (right, 22.0f, right, 78.0f, 3.0f);
    c.stroke (wavePath (c, left + 6.0f, right - 6.0f, 50.0f, 10.0f, 3.0f), 2.0f);
    c.colour (c.p.vermilion);
    c.line (36.0f, 84.0f, 64.0f, 84.0f, 2.0f);
    c.arrowHead (31.0f, 84.0f, juce::MathConstants<float>::pi, 5.0f);
    c.arrowHead (69.0f, 84.0f, 0.0f, 5.0f);
}

void drawPitch (GlyphCanvas& c, float phase, bool up)
{
    const float direction = up ? -1.0f : 1.0f;
    juce::Path steps;
    steps.startNewSubPath (c.pt (18.0f, up ? 72.0f : 28.0f));
    for (int i = 0; i < 4; ++i)
    {
        const float nextX = 18.0f + (i + 1) * 16.0f;
        const float y0 = (up ? 72.0f : 28.0f) + direction * i * 13.0f;
        const float y1 = y0 + direction * 13.0f;
        steps.lineTo (c.pt (nextX, y0));
        steps.lineTo (c.pt (nextX, y1));
    }
    c.colour (c.p.ink);
    c.stroke (steps, 2.0f);
    c.line (16.0f, up ? 20.0f : 80.0f, 86.0f, up ? 20.0f : 80.0f, 1.0f);
    const float travel = wrapped (phase) * 4.0f;
    const int step = juce::jlimit (0, 3, static_cast<int> (travel));
    c.dot (26.0f + step * 16.0f,
           (up ? 65.5f : 34.5f) + direction * step * 13.0f,
           3.8f, c.p.vermilion);
}

void drawSliceBlocks (GlyphCanvas& c, float phase, ModifierType type, const ModifierDescriptor& d)
{
    int count = 4;
    if (type == ModifierType::BeatSliceRandom && d.plannedSliceDivision.isNotEmpty())
        count = juce::jlimit (4, 8, d.plannedSliceDivision.getTrailingIntValue());
    const float gap = 3.0f;
    const float width = (76.0f - gap * (count - 1)) / static_cast<float> (count);
    const float traversal = wrapped (phase) * static_cast<float> (count);
    const int activeStep = static_cast<int> (traversal) % count;
    bool activeVisible = true;
    if (type == ModifierType::SliceRepeater)
    {
        // Repeat the selected slice twice (on/off/on/off) before choosing the
        // next shuffled slice.
        const float repeatPhase = traversal - std::floor (traversal);
        activeVisible = static_cast<int> (repeatPhase * 4.0f) % 2 == 0;
    }

    // All slice glyphs traverse a deterministic shuffled permutation rather
    // than reading as a conventional left-to-right playhead scan.
    const int active = activeVisible ? (activeStep * 3) % count : -1;
    for (int i = 0; i < count; ++i)
    {
        int order = i;
        if (type == ModifierType::BeatSliceRandom)
            order = (i * 3) % count;
        const float height = type == ModifierType::ArpSlice ? 18.0f + order * (36.0f / juce::jmax (1, count - 1)) : 34.0f;
        const float top = 68.0f - height + ((type == ModifierType::BeatSliceRandom && i % 2) ? -8.0f : 0.0f);
        auto box = c.rect (12.0f + i * (width + gap), top, width, height);
        c.colour (i == active ? c.p.vermilion : c.p.ink, i == active ? 1.0f : 0.78f);
        if (type == ModifierType::SliceRepeater && i != active)
            c.g.drawRect (box, c.u (1.5f));
        else
            c.g.fillRect (box);
    }
    c.colour (c.p.mutedInk);
    c.line (10.0f, 76.0f, 90.0f, 76.0f, 1.0f);
}

void drawPingPong (GlyphCanvas& c, float phase)
{
    c.colour (c.p.ink);
    c.line (18.0f, 25.0f, 18.0f, 75.0f, 3.0f);
    c.line (82.0f, 25.0f, 82.0f, 75.0f, 3.0f);
    c.line (25.0f, 50.0f, 75.0f, 50.0f, 1.0f);
    c.arrowHead (23.0f, 50.0f, juce::MathConstants<float>::pi, 5.0f);
    c.arrowHead (77.0f, 50.0f, 0.0f, 5.0f);
    c.dot (22.0f + triangle (phase) * 56.0f, 50.0f, 5.0f, c.p.vermilion);
}

void drawDelay (GlyphCanvas& c, float phase)
{
    constexpr float centreX = 50.0f;
    constexpr float baselineY = 70.0f; // centres the visible upper semicircles optically
    constexpr int rings = 4;
    for (int i = 0; i < rings; ++i)
    {
        const float expand = wrapped (phase) * 5.0f;
        const float radius = 10.0f + i * 9.0f + expand;
        c.colour (i == 0 ? c.p.vermilion : c.p.ink, 1.0f - i * 0.14f);
        juce::Path arc;
        arc.addCentredArc (c.x (centreX), c.y (baselineY), c.u (radius), c.u (radius), 0.0f,
                           -juce::MathConstants<float>::halfPi,
                           juce::MathConstants<float>::halfPi, true);
        c.stroke (arc, i == 0 ? 3.0f : 2.0f);
    }
    c.dot (centreX, baselineY, 4.0f, c.p.vermilion);
}

void drawDubDelay (GlyphCanvas& c, float phase)
{
    // Dub is shown as a feedback/send instrument: a dense tape-like coil is
    // charged on the left, opened through a gate, then released as spaced taps.
    constexpr float coilX = 36.0f;
    constexpr float coilY = 52.0f;
    constexpr float gateX = 64.0f;
    const float cycle = wrapped (phase);
    const float charge = triangle (cycle);

    juce::Path coil;
    constexpr int coilPoints = 96;
    for (int i = 0; i <= coilPoints; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (coilPoints);
        const float angle = (-0.5f + t * (2.7f + charge * 0.25f))
                          * juce::MathConstants<float>::twoPi;
        const float radius = 2.5f + t * 22.0f;
        const auto point = c.pt (coilX + std::cos (angle) * radius,
                                 coilY + std::sin (angle) * radius * 0.78f);
        if (i == 0) coil.startNewSubPath (point); else coil.lineTo (point);
    }
    c.colour (c.p.ink);
    c.stroke (coil, 2.2f);

    // A small moving input pulse makes the coil feel charged rather than floral.
    const float inputAngle = cycle * juce::MathConstants<float>::twoPi * 2.0f;
    c.dot (coilX + std::cos (inputAngle) * 8.0f,
           coilY + std::sin (inputAngle) * 6.0f, 3.2f, c.p.vermilion);

    // Pressure gate.  It opens widest during the release half of the cycle.
    const float gateOpen = 5.0f + charge * 5.0f;
    c.colour (c.p.ink);
    c.line (gateX, 24.0f, gateX, coilY - gateOpen, 2.5f);
    c.line (gateX, coilY + gateOpen, gateX, 80.0f, 2.5f);
    c.line (gateX - 5.0f, 24.0f, gateX + 5.0f, 24.0f, 1.5f);
    c.line (gateX - 5.0f, 80.0f, gateX + 5.0f, 80.0f, 1.5f);
    c.line (gateX + 3.0f, coilY, 91.0f, coilY, 1.0f);

    // Red, gold, and green read as discrete echo taps, with spacing expanding
    // as the feedback charge is released.
    const juce::Colour tapColours[] = { c.p.vermilion, c.p.safetyYellow, c.p.signalGreen };
    for (int i = 0; i < 3; ++i)
    {
        const float spacing = 7.0f + charge * 2.5f;
        const float tapX = gateX + 7.0f + i * spacing;
        const float radius = 3.8f - i * 0.55f;
        c.dot (tapX, coilY, radius, tapColours[i]);
    }

    // Sparse return path closes the feedback loop without a decorative burst.
    juce::Path feedback;
    feedback.startNewSubPath (c.pt (89.0f, 44.0f));
    feedback.lineTo (c.pt (89.0f, 18.0f));
    feedback.lineTo (c.pt (44.0f, 18.0f));
    feedback.lineTo (c.pt (44.0f, 25.0f));
    c.colour (c.p.mutedInk, 0.75f);
    c.stroke (feedback, 1.25f);
    c.arrowHead (44.0f, 28.0f, juce::MathConstants<float>::halfPi, 4.0f);
}

void drawReverb (GlyphCanvas& c, float phase)
{
    for (int i = 0; i < 5; ++i)
    {
        c.colour (i == 0 ? c.p.vermilion : c.p.ink, 0.95f - i * 0.13f);
        c.stroke (irregularRing (c, 50.0f, 50.0f, 9.0f + i * 8.0f,
                                 0.7f + i * 0.35f, phase + i * 0.11f),
                  i == 0 ? 2.5f : 1.5f);
    }
    c.dot (50.0f, 50.0f, 3.5f, c.p.vermilion);
}

void drawFilter (GlyphCanvas& c, float phase, bool highPass, bool sampleHold, bool master)
{
    if (! sampleHold)
    {
        c.colour (c.p.ink);
        juce::Path terrain;
        terrain.startNewSubPath (c.pt (14.0f, highPass ? 70.0f : 26.0f));
        for (int i = 0; i < 5; ++i)
        {
            const float nx = 14.0f + (i + 1) * 13.0f;
            const float y0 = (highPass ? 70.0f : 26.0f) + (highPass ? -1.0f : 1.0f) * i * 9.0f;
            const float y1 = y0 + (highPass ? -9.0f : 9.0f);
            terrain.lineTo (c.pt (nx, y0));
            terrain.lineTo (c.pt (nx, y1));
        }
        c.stroke (terrain, 1.5f);
    }
    else
    {
        // One discrete cutoff trace only.  It changes in held snapshots rather
        // than gliding, and uses irregular widths/heights to avoid a repeating
        // staircase rhythm.
        constexpr float posts[] = { 12.0f, 21.0f, 35.0f, 43.0f,
                                    59.0f, 70.0f, 84.0f, 91.0f };
        constexpr int numPosts = static_cast<int> (std::size (posts));
        const int heldFrame = static_cast<int> (wrapped (phase) * 5.0f);
        float values[numPosts] {};
        for (int i = 0; i < numPosts; ++i)
        {
            const int hash = (i * 43 + i * i * 7 + heldFrame * 29 + 11) % 67;
            values[i] = 17.0f + static_cast<float> (hash);
        }

        juce::Path holds;
        holds.startNewSubPath (c.pt (posts[0], values[0]));
        for (int i = 0; i < numPosts - 1; ++i)
        {
            holds.lineTo (c.pt (posts[i + 1], values[i]));
            holds.lineTo (c.pt (posts[i + 1], values[i + 1]));
        }
        c.colour (c.p.ink);
        c.stroke (holds, 2.0f);
    }

    const float sweep = (triangle (phase) - 0.5f) * 8.0f;
    c.colour (c.p.vermilion);
    c.line (20.0f, highPass ? 78.0f - sweep : 20.0f + sweep,
            82.0f, highPass ? 20.0f + sweep : 82.0f - sweep, 2.5f);

    if (master)
    {
        c.colour (c.p.mutedInk);
        for (int i = 0; i < 8; ++i)
            c.line (18.0f + i * 9.0f, 88.0f, 18.0f + i * 9.0f, 93.0f, 1.5f);
    }
}

void drawVolumeRamp (GlyphCanvas& c, float phase)
{
    const float voiceLevel = 1.0f - wrapped (phase);

    // Librarian: bun, head, glasses, shoulders, and the raised shushing finger.
    c.colour (c.p.ink);
    c.g.drawEllipse (c.rect (19.0f, 23.0f, 27.0f, 31.0f), c.u (2.0f));
    c.g.fillEllipse (c.rect (14.0f, 20.0f, 11.0f, 11.0f));
    c.g.drawEllipse (c.rect (23.0f, 31.0f, 8.0f, 7.0f), c.u (1.5f));
    c.g.drawEllipse (c.rect (33.0f, 31.0f, 8.0f, 7.0f), c.u (1.5f));
    c.line (31.0f, 34.5f, 33.0f, 34.5f, 1.2f);
    c.dot (27.0f, 34.5f, 0.8f, c.p.ink);
    c.dot (37.0f, 34.5f, 0.8f, c.p.ink);
    c.line (37.0f, 45.0f, 41.0f, 45.0f, 1.3f);

    juce::Path librarianBody;
    librarianBody.startNewSubPath (c.pt (12.0f, 82.0f));
    librarianBody.lineTo (c.pt (17.0f, 63.0f));
    librarianBody.lineTo (c.pt (27.0f, 56.0f));
    librarianBody.lineTo (c.pt (40.0f, 58.0f));
    librarianBody.lineTo (c.pt (49.0f, 82.0f));
    c.stroke (librarianBody, 2.0f);

    juce::Path shushingArm;
    shushingArm.startNewSubPath (c.pt (27.0f, 70.0f));
    shushingArm.lineTo (c.pt (43.0f, 57.0f));
    shushingArm.lineTo (c.pt (43.0f, 39.0f));
    c.colour (c.p.vermilion);
    c.stroke (shushingArm, 2.8f);
    c.dot (43.0f, 38.0f, 1.8f, c.p.vermilion);

    // Talker: a simpler profile facing the librarian.
    c.colour (c.p.ink);
    c.g.drawEllipse (c.rect (68.0f, 33.0f, 18.0f, 21.0f), c.u (2.0f));
    c.line (68.0f, 42.0f, 64.0f, 45.0f, 1.5f);
    c.line (64.0f, 45.0f, 68.0f, 47.0f, 1.5f);
    c.dot (73.0f, 40.0f, 1.0f, c.p.ink);

    juce::Path talkerBody;
    talkerBody.startNewSubPath (c.pt (58.0f, 82.0f));
    talkerBody.lineTo (c.pt (63.0f, 63.0f));
    talkerBody.lineTo (c.pt (76.0f, 57.0f));
    talkerBody.lineTo (c.pt (88.0f, 64.0f));
    talkerBody.lineTo (c.pt (92.0f, 82.0f));
    c.stroke (talkerBody, 2.0f);

    // The talker's marks contract and disappear as the librarian shushes them.
    c.colour (c.p.safetyYellow, 0.20f + voiceLevel * 0.80f);
    for (int i = 0; i < 3; ++i)
    {
        const float length = (7.0f - i * 1.5f) * voiceLevel;
        const float y = 38.0f + i * 6.0f;
        c.line (61.0f - length, y, 61.0f, y, 1.6f);
    }
}

void drawTremolo (GlyphCanvas& c, float phase)
{
    c.colour (c.p.mutedInk);
    c.line (12.0f, 50.0f, 88.0f, 50.0f, 1.0f);
    c.colour (c.p.ink);
    const float amp = 12.0f + triangle (phase) * 9.0f;
    c.stroke (wavePath (c, 12.0f, 88.0f, 50.0f, amp, 4.0f), 2.5f);
    c.dot (50.0f, 50.0f, 3.5f, c.p.vermilion);
}

void drawChorus (GlyphCanvas& c, float phase)
{
    const juce::Colour colours[] = { c.p.ultramarine, c.p.ink, c.p.vermilion };
    for (int i = 0; i < 3; ++i)
    {
        c.colour (colours[i], i == 1 ? 1.0f : 0.78f);
        c.stroke (wavePath (c, 12.0f, 88.0f, 42.0f + i * 8.0f, 8.0f, 2.0f,
                            phase * (i == 1 ? 0.0f : (i == 0 ? 0.25f : -0.25f)) + i * 0.12f), 2.0f);
    }
}

void drawAutoPan (GlyphCanvas& c, float phase)
{
    c.colour (c.p.ink);
    juce::Path arc;
    arc.addCentredArc (c.x (50.0f), c.y (50.0f), c.u (34.0f), c.u (24.0f), 0.0f,
                       -juce::MathConstants<float>::pi, juce::MathConstants<float>::pi, true);
    c.stroke (arc, 2.0f);
    c.line (14.0f, 30.0f, 14.0f, 70.0f, 2.0f);
    c.line (86.0f, 30.0f, 86.0f, 70.0f, 2.0f);
    c.dot (18.0f + triangle (phase) * 64.0f, 50.0f, 5.0f, c.p.vermilion);
}

void drawDucking (GlyphCanvas& c, float phase)
{
    c.colour (c.p.ink);
    c.stroke (wavePath (c, 12.0f, 88.0f, 62.0f, 7.0f, 4.0f), 2.0f);
    const float pulseX = 20.0f + wrapped (phase) * 60.0f;
    c.colour (c.p.vermilion);
    c.line (pulseX, 18.0f, pulseX, 42.0f, 4.0f);
    juce::Path notch;
    notch.startNewSubPath (c.pt (pulseX - 10.0f, 58.0f));
    notch.lineTo (c.pt (pulseX, 78.0f));
    notch.lineTo (c.pt (pulseX + 10.0f, 58.0f));
    c.stroke (notch, 2.5f);
}

void drawGranular (GlyphCanvas& c, float phase, bool momentary, const ModifierDescriptor& d)
{
    int count = momentary ? 15 : 22;
    if (d.plannedGrainDensityHz.has_value())
        count = juce::jlimit (10, 34, static_cast<int> (d.plannedGrainDensityHz.value() * 1.2));
    const float burst = momentary ? triangle (phase) : 0.45f + triangle (phase) * 0.35f;
    for (int i = 0; i < count; ++i)
    {
        const float a = static_cast<float> (i) * 2.399963f + phase * 0.8f;
        const float radial = (10.0f + (i % 9) * 4.0f) * burst;
        const float px = 50.0f + std::cos (a) * radial;
        const float py = 50.0f + std::sin (a) * radial * 0.72f;
        c.dot (px, py, 1.4f + (i % 3) * 0.45f, i % 5 == 0 ? c.p.vermilion : c.p.ink);
    }
    c.colour (c.p.mutedInk);
    c.stroke (wavePath (c, 24.0f, 76.0f, 50.0f, 5.0f, 2.0f), 1.5f);
}

void drawSwitchPart (GlyphCanvas& c, float phase)
{
    const float shift = triangle (phase) * 6.0f;
    c.colour (c.p.mutedInk);
    c.g.drawRect (c.rect (34.0f, 24.0f, 42.0f, 48.0f), c.u (1.5f));
    c.colour (c.p.ink);
    c.g.fillRect (c.rect (22.0f + shift, 32.0f, 42.0f, 48.0f));
    c.colour (c.p.raisedTile);
    c.g.setFont (juce::Font (juce::FontOptions().withHeight (c.u (22.0f))).boldened());
    c.g.drawText ("02", c.rect (22.0f + shift, 32.0f, 42.0f, 48.0f), juce::Justification::centred);
    c.colour (c.p.vermilion);
    c.line (38.0f, 88.0f, 72.0f, 88.0f, 2.0f);
    c.arrowHead (77.0f, 88.0f, 0.0f, 5.0f);
}

void drawQuarterBurst (GlyphCanvas& c, float phase)
{
    c.colour (c.p.ink);
    c.line (12.0f, 58.0f, 88.0f, 58.0f, 2.0f);
    const int active = static_cast<int> (wrapped (phase) * 4.0f) % 4;
    for (int i = 0; i < 4; ++i)
    {
        const float px = 20.0f + i * 20.0f;
        c.dot (px, 58.0f, i == active ? 5.0f : 3.0f, i == active ? c.p.vermilion : c.p.ink);
        c.colour (i == active ? c.p.vermilion : c.p.mutedInk);
        c.line (px, 30.0f, px, 47.0f, i == active ? 3.0f : 1.5f);
    }
}

void drawSwap (GlyphCanvas& c, float phase)
{
    const float shift = triangle (phase) * 8.0f;
    c.colour (c.p.ink);
    c.g.drawRect (c.rect (12.0f, 22.0f, 27.0f, 54.0f), c.u (2.0f));
    c.g.drawRect (c.rect (61.0f, 22.0f, 27.0f, 54.0f), c.u (2.0f));
    for (int i = 0; i < 3; ++i)
    {
        c.colour (i == 0 ? c.p.vermilion : (i == 1 ? c.p.ultramarine : c.p.safetyYellow));
        c.g.fillRect (c.rect (17.0f + shift * (i == 0 ? 1.0f : 0.0f), 30.0f + i * 14.0f, 17.0f, 6.0f));
        c.g.fillRect (c.rect (66.0f - shift * (i == 2 ? 1.0f : 0.0f), 30.0f + i * 14.0f, 17.0f, 6.0f));
    }
    c.colour (c.p.ink);
    c.line (34.0f, 18.0f, 66.0f, 82.0f, 1.5f);
    c.line (34.0f, 82.0f, 66.0f, 18.0f, 1.5f);
}

void drawReset (GlyphCanvas& c, float phase)
{
    const float retract = 1.0f - triangle (phase) * 0.2f;
    const juce::Colour colours[] = { c.p.vermilion, c.p.safetyYellow, c.p.signalGreen, c.p.ultramarine };
    for (int i = 0; i < 4; ++i)
    {
        const float a = juce::MathConstants<float>::halfPi * i
                      + juce::MathConstants<float>::pi * 0.25f;
        const float sx = 50.0f + std::cos (a) * 38.0f * retract;
        const float sy = 50.0f + std::sin (a) * 38.0f * retract;
        c.colour (colours[i]);
        c.line (sx, sy, 55.0f + std::cos (a) * 2.0f, 55.0f + std::sin (a) * 2.0f, 2.5f);
        c.arrowHead (50.0f + std::cos (a) * 7.0f, 50.0f + std::sin (a) * 7.0f, a + juce::MathConstants<float>::pi, 5.0f);
    }
    c.dot (50.0f, 50.0f, 5.0f, c.p.ink);
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

    const float phase = state.reducedMotion ? 0.25f : wrapped (state.phase01);
    GlyphCanvas c (g, bounds, palette, state.emphasis01);

    switch (state.descriptor.type)
    {
        case ModifierType::Reverse:                    drawReverse (c, phase); break;
        case ModifierType::Speed:                      drawSpeed (c, phase); break;
        case ModifierType::Stretch:                    drawStretch (c, phase); break;
        case ModifierType::PitchUpOctave:              drawPitch (c, phase, true); break;
        case ModifierType::PitchDownOctave:            drawPitch (c, phase, false); break;
        case ModifierType::BeatSliceRandom:
        case ModifierType::ArpSlice:
        case ModifierType::SliceRepeater:               drawSliceBlocks (c, phase, state.descriptor.type, state.descriptor); break;
        case ModifierType::PingPong:                    drawPingPong (c, phase); break;
        case ModifierType::BufferDelayOn:               drawDelay (c, phase); break;
        case ModifierType::BufferDelayDubBurst:         drawDubDelay (c, phase); break;
        case ModifierType::BufferReverbOn:              drawReverb (c, phase); break;
        case ModifierType::BufferLowPassOn:             drawFilter (c, phase, false, false, false); break;
        case ModifierType::BufferHighPassOn:            drawFilter (c, phase, true, false, false); break;
        case ModifierType::BufferVolumeRampDown:        drawVolumeRamp (c, phase); break;
        case ModifierType::BufferTremolo:               drawTremolo (c, phase); break;
        case ModifierType::BufferChorusOn:              drawChorus (c, phase); break;
        case ModifierType::BufferAutoPan:               drawAutoPan (c, phase); break;
        case ModifierType::BufferDuckingOn:             drawDucking (c, phase); break;
        case ModifierType::BufferSHLowPassOn:           drawFilter (c, phase, false, true, false); break;
        case ModifierType::BufferSHHighPassOn:          drawFilter (c, phase, true, true, false); break;
        case ModifierType::BufferGranularOn:            drawGranular (c, phase, false, state.descriptor); break;
        case ModifierType::BufferGranularMomentary:     drawGranular (c, phase, true, state.descriptor); break;
        case ModifierType::MasterHighPassOn:            drawFilter (c, phase, true, false, true); break;
        case ModifierType::MasterLowPassOn:             drawFilter (c, phase, false, false, true); break;
        case ModifierType::SwitchPart:                  drawSwitchPart (c, phase); break;
        case ModifierType::QuarterNoteBurst:            drawQuarterBurst (c, phase); break;
        case ModifierType::SwapModifierStack:           drawSwap (c, phase); break;
        case ModifierType::ResetAll:                    drawReset (c, phase); break;
        case ModifierType::Unknown:                     drawUnknown (c); break;
    }
}
