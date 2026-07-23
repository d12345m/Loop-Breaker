#if JUCE_UNIT_TESTS

#include <JuceHeader.h>

#include "ModifierRegistry.h"
#include "ModifierStickerOverlay.h"

#include <array>
#include <vector>

namespace
{
constexpr std::array<ModifierType, ModifierStickerOverlay::stickerCount> expectedOrder {{
    ModifierType::Reverse,
    ModifierType::Speed,
    ModifierType::Stretch,
    ModifierType::PitchUpOctave,
    ModifierType::PitchDownOctave,
    ModifierType::BeatSliceRandom,
    ModifierType::ArpSlice,
    ModifierType::SliceRepeater,
    ModifierType::PingPong,
    ModifierType::BufferDelayOn,
    ModifierType::BufferDelayDubBurst,
    ModifierType::BufferReverbOn,
    ModifierType::BufferLowPassOn,
    ModifierType::BufferHighPassOn,
    ModifierType::BufferShhhhhh,
    ModifierType::BufferTremolo,
    ModifierType::BufferChorusOn,
    ModifierType::BufferAutoPan,
    ModifierType::BufferDuckingOn,
    ModifierType::BufferSHLowPassOn,
    ModifierType::BufferSHHighPassOn,
    ModifierType::BufferGranularOn,
    ModifierType::BufferGranularMomentary,
    ModifierType::SwitchPart,
    ModifierType::QuarterNoteBurst,
    ModifierType::SwapModifierStack,
    ModifierType::ResetAll,
}};

class ModifierStickerOverlayTest : public juce::UnitTest
{
public:
    ModifierStickerOverlayTest()
        : juce::UnitTest ("Modifier Sticker Overlay", "LoopBreaker")
    {
    }

    void runTest() override
    {
        beginTest ("Canonical registry order maps deterministically to sticker slots");

        std::vector<ModifierType> registryOrder;
        for (const auto& entry : ModifierRegistry::entries())
            if (entry.type != ModifierType::MasterHighPassOn
                && entry.type != ModifierType::MasterLowPassOn)
                registryOrder.push_back (entry.type);

        expectEquals (static_cast<int> (registryOrder.size()),
                      ModifierStickerOverlay::stickerCount);

        for (int slot = 0; slot < ModifierStickerOverlay::stickerCount; ++slot)
        {
            const auto type = expectedOrder[static_cast<size_t> (slot)];
            expectEquals (static_cast<int> (registryOrder[static_cast<size_t> (slot)]),
                          static_cast<int> (type));
            expectEquals (ModifierStickerOverlay::slotForType (type), slot);
            expectEquals (ModifierStickerOverlay::slotForType (type), slot);
        }

        expectEquals (ModifierStickerOverlay::slotForType (ModifierType::MasterHighPassOn), -1);
        expectEquals (ModifierStickerOverlay::slotForType (ModifierType::MasterLowPassOn), -1);
        expectEquals (ModifierStickerOverlay::slotForType (ModifierType::Unknown), -1);

        beginTest ("Mask bits are unique and correspond to canonical slots");

        ModifierStickerOverlay::Mask accumulated {};
        for (int slot = 0; slot < ModifierStickerOverlay::stickerCount; ++slot)
        {
            const auto bit = ModifierStickerOverlay::bitForType (
                expectedOrder[static_cast<size_t> (slot)]);
            const auto expectedBit = ModifierStickerOverlay::Mask { 1 }
                                   << static_cast<unsigned int> (slot);

            expect (bit == expectedBit);
            expect ((accumulated & bit) == 0);
            accumulated |= bit;
        }

        const auto allExpectedBits = (ModifierStickerOverlay::Mask { 1 }
                                      << ModifierStickerOverlay::stickerCount) - 1;
        expect (accumulated == allExpectedBits);
        expect (ModifierStickerOverlay::bitForType (ModifierType::MasterHighPassOn) == 0);
        expect (ModifierStickerOverlay::bitForType (ModifierType::MasterLowPassOn) == 0);
        expect (ModifierStickerOverlay::bitForType (ModifierType::Unknown) == 0);

        beginTest ("Landscape stickers fill rightward from the bottom-left");
        verifyBounds ({ 13.0f, 17.0f, 620.0f, 248.0f });

        beginTest ("Portrait stickers fill rightward from the bottom-left");
        verifyBounds ({ 7.0f, 11.0f, 178.0f, 420.0f });
    }

private:
    void verifyBounds (juce::Rectangle<float> aperture)
    {
        std::array<juce::Rectangle<float>, ModifierStickerOverlay::stickerCount> bounds;

        for (int slot = 0; slot < ModifierStickerOverlay::stickerCount; ++slot)
        {
            const auto type = expectedOrder[static_cast<size_t> (slot)];
            const auto first = ModifierStickerOverlay::getStickerBounds (aperture, type);
            const auto second = ModifierStickerOverlay::getStickerBounds (aperture, type);
            bounds[static_cast<size_t> (slot)] = first;

            expect (first == second, "Sticker layout must be deterministic");
            expect (! first.isEmpty());
            expect (first.getWidth() > 0.0f && first.getHeight() > 0.0f);
            expect (aperture.contains (first),
                    "Sticker bounds must remain inside the supplied aperture");

            const int expectedColumn = slot % ModifierStickerOverlay::columns;
            const int expectedRowFromTop =
                ModifierStickerOverlay::rows - 1
                - slot / ModifierStickerOverlay::columns;
            const float cellWidth = aperture.getWidth()
                                  / static_cast<float> (ModifierStickerOverlay::columns);
            const float cellHeight = aperture.getHeight()
                                   / static_cast<float> (ModifierStickerOverlay::rows);

            expectEquals (static_cast<int> ((first.getCentreX() - aperture.getX())
                                            / cellWidth),
                          expectedColumn);
            expectEquals (static_cast<int> ((first.getCentreY() - aperture.getY())
                                            / cellHeight),
                          expectedRowFromTop);
        }

        for (int first = 0; first < ModifierStickerOverlay::stickerCount; ++first)
            for (int second = first + 1;
                 second < ModifierStickerOverlay::stickerCount;
                 ++second)
                expect (! bounds[static_cast<size_t> (first)]
                              .intersects (bounds[static_cast<size_t> (second)]),
                        "Sticker bounds must not overlap");

        expect (ModifierStickerOverlay::getStickerBounds (
                    aperture, ModifierType::MasterHighPassOn).isEmpty());
        expect (ModifierStickerOverlay::getStickerBounds (
                    aperture, ModifierType::MasterLowPassOn).isEmpty());
        expect (ModifierStickerOverlay::getStickerBounds (
                    aperture, ModifierType::Unknown).isEmpty());
    }
};

static ModifierStickerOverlayTest modifierStickerOverlayTestInstance;
}

#endif
