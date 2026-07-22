#pragma once

#include <JuceHeader.h>

#include "Modifier.h"

#include <cmath>

namespace ModifierVariantFormatter
{
inline juce::String percent (double value)
{
    return juce::String (juce::roundToInt (value * 100.0)) + "%";
}

inline juce::String bars (double value)
{
    if (value <= 0.0)
        return "INSTANT";

    const auto rounded = juce::roundToInt (value);
    return juce::String (rounded) + (rounded == 1 ? " BAR" : " BARS");
}

inline juce::String noteDivision (double divisionInBars, bool abbreviated)
{
    if (divisionInBars >= 1.0)    return abbreviated ? "1/1" : "WHOLE NOTE";
    if (divisionInBars >= 0.5)    return abbreviated ? "1/2" : "HALF NOTE";
    if (divisionInBars >= 0.25)   return abbreviated ? "1/4" : "QUARTER NOTE";
    if (divisionInBars >= 0.125)  return "1/8";
    return "1/16";
}

inline juce::String destinationPart (int zeroBasedPart)
{
    return "PART " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + juce::jlimit (0, 25, zeroBasedPart)));
}

inline juce::String full (const ModifierDescriptor& descriptor)
{
    switch (descriptor.type)
    {
        case ModifierType::Speed:
            return descriptor.plannedSpeed.has_value()
                       ? juce::String (*descriptor.plannedSpeed, 2) + "X" : juce::String();
        case ModifierType::Stretch:
            return descriptor.plannedStretch.has_value()
                       ? juce::String (*descriptor.plannedStretch, 2) + "X" : juce::String();
        case ModifierType::PitchUpOctave:   return "+1 OCT";
        case ModifierType::PitchDownOctave: return "-1 OCT";
        case ModifierType::BeatSliceRandom:
            return descriptor.plannedSliceDivision.isNotEmpty()
                       ? descriptor.plannedSliceDivision + " SLICES" : juce::String();
        case ModifierType::ArpSlice:
        {
            juce::String result;
            if (descriptor.plannedArpSequenceLength.has_value())
                result << *descriptor.plannedArpSequenceLength << " STEP";
            if (descriptor.plannedArpTotalSlices.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << *descriptor.plannedArpTotalSlices << " GRID";
            if (descriptor.plannedArpRepeatBars.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << bars (*descriptor.plannedArpRepeatBars);
            return result;
        }
        case ModifierType::SliceRepeater:
        {
            juce::String result;
            if (descriptor.plannedSliceRepeaterReps.has_value())
                result << *descriptor.plannedSliceRepeaterReps << " REPEATS";
            if (descriptor.plannedSliceRepeaterTotal.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << *descriptor.plannedSliceRepeaterTotal << " GRID";
            return result;
        }
        case ModifierType::PingPong:
            return descriptor.plannedPingPongDivision.has_value()
                       ? noteDivision (*descriptor.plannedPingPongDivision, false) : juce::String();
        case ModifierType::BufferDelayOn:
        case ModifierType::BufferDelayDubBurst:
        {
            juce::String result;
            if (! descriptor.plannedDelayDivisions.isEmpty())
                result << descriptor.plannedDelayDivisions.joinIntoString (",");
            else if (descriptor.plannedDelayDivision.isNotEmpty())
                result << descriptor.plannedDelayDivision;
            if (descriptor.plannedDelayWet.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << "WET " << percent (*descriptor.plannedDelayWet);
            if (descriptor.plannedDelayFeedback.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << "FB " << percent (*descriptor.plannedDelayFeedback);
            if (descriptor.plannedFxFadeBars.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << bars (*descriptor.plannedFxFadeBars);
            if (descriptor.plannedDelayPingPong.value_or (false)) result << "  /  PING-PONG";
            if (descriptor.plannedWowFlutter.value_or (false))   result << "  /  WOW";
            return result;
        }
        case ModifierType::BufferReverbOn:
        {
            juce::String result;
            if (descriptor.plannedWet.has_value())
                result << "WET " << percent (*descriptor.plannedWet);
            if (descriptor.plannedFxFadeBars.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << bars (*descriptor.plannedFxFadeBars);
            return result;
        }
        case ModifierType::BufferLowPassOn:
        case ModifierType::BufferHighPassOn:
        case ModifierType::MasterLowPassOn:
        case ModifierType::MasterHighPassOn:
        {
            juce::String result;
            if (descriptor.plannedFxFadeBars.has_value())
                result << bars (*descriptor.plannedFxFadeBars);
            if (descriptor.plannedImmediateJump.has_value())
                result << (result.isNotEmpty() ? "  /  " : "")
                       << (*descriptor.plannedImmediateJump ? "JUMP THEN DECAY" : "RAMP UP / DOWN");
            return result;
        }
        case ModifierType::BufferVolumeRampDown:
            return descriptor.plannedFxFadeBars.has_value() ? bars (*descriptor.plannedFxFadeBars) : juce::String();
        case ModifierType::BufferChorusOn:
        {
            juce::String result;
            if (descriptor.plannedChorusMix.has_value())
                result << "MIX " << percent (*descriptor.plannedChorusMix);
            if (descriptor.plannedChorusDepth.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << "DEPTH " << percent (*descriptor.plannedChorusDepth);
            if (descriptor.plannedChorusRateHz.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << juce::String (*descriptor.plannedChorusRateHz, 1) << " HZ";
            if (descriptor.plannedFxFadeBars.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << bars (*descriptor.plannedFxFadeBars);
            return result;
        }
        case ModifierType::BufferAutoPan:
        {
            juce::String result;
            if (descriptor.plannedPanMix.has_value())
                result << "MIX " << percent (*descriptor.plannedPanMix);
            if (descriptor.plannedPanDepth.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << "DEPTH " << percent (*descriptor.plannedPanDepth);
            if (descriptor.plannedPanRateHz.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << juce::String (*descriptor.plannedPanRateHz, 2) << " HZ";
            if (descriptor.plannedFxFadeBars.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << bars (*descriptor.plannedFxFadeBars);
            return result;
        }
        case ModifierType::BufferSHLowPassOn:
        case ModifierType::BufferSHHighPassOn:
            return descriptor.plannedSHDivisionBars.has_value()
                       ? "HOLD " + noteDivision (*descriptor.plannedSHDivisionBars, true) : juce::String();
        case ModifierType::BufferGranularOn:
        case ModifierType::BufferGranularMomentary:
        {
            juce::String result;
            if (descriptor.plannedGrainDensityHz.has_value())
                result << juce::String (*descriptor.plannedGrainDensityHz, 0) << " G/S";
            if (descriptor.plannedGrainSizeMs.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << juce::String (*descriptor.plannedGrainSizeMs, 0) << " MS";
            if (descriptor.plannedGrainPitchSpread.has_value())
            {
                const auto spread = *descriptor.plannedGrainPitchSpread;
                result << (result.isNotEmpty() ? "  /  " : "")
                       << (spread <= 0.0 ? juce::String ("UNISON")
                                         : juce::String (juce::CharPointer_UTF8 ("\xc2\xb1"))
                                             + juce::String (juce::roundToInt (spread / 12.0)) + " OCT");
            }
            if (descriptor.plannedGrainMix.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << "MIX " << percent (*descriptor.plannedGrainMix);
            if (descriptor.plannedFxFadeBars.has_value())
                result << (result.isNotEmpty() ? "  /  " : "") << bars (*descriptor.plannedFxFadeBars);
            return result;
        }
        case ModifierType::SwitchPart:
            return descriptor.plannedDestinationPart.has_value()
                       ? destinationPart (*descriptor.plannedDestinationPart) : juce::String();
        case ModifierType::QuarterNoteBurst:
            return descriptor.plannedBurstBars.has_value() ? bars (*descriptor.plannedBurstBars) : juce::String();
        case ModifierType::Reverse:
        case ModifierType::BufferTremolo:
        case ModifierType::BufferDuckingOn:
        case ModifierType::SwapModifierStack:
        case ModifierType::ResetAll:
        case ModifierType::Unknown:
            return {};
    }

    return {};
}

inline juce::String compact (const ModifierDescriptor& descriptor)
{
    switch (descriptor.type)
    {
        case ModifierType::ArpSlice:
            if (descriptor.plannedArpSequenceLength.has_value() && descriptor.plannedArpTotalSlices.has_value())
                return juce::String (*descriptor.plannedArpSequenceLength) + "/" + juce::String (*descriptor.plannedArpTotalSlices);
            break;
        case ModifierType::SliceRepeater:
            if (descriptor.plannedSliceRepeaterReps.has_value() && descriptor.plannedSliceRepeaterTotal.has_value())
                return juce::String (*descriptor.plannedSliceRepeaterReps) + "X / " + juce::String (*descriptor.plannedSliceRepeaterTotal);
            break;
        case ModifierType::BufferDelayOn:
        case ModifierType::BufferDelayDubBurst:
            if (descriptor.plannedDelayDivision.isNotEmpty())
                return descriptor.plannedDelayDivision;
            break;
        case ModifierType::BufferReverbOn:
            if (descriptor.plannedWet.has_value())
                return "WET " + percent (*descriptor.plannedWet);
            break;
        case ModifierType::BufferChorusOn:
            if (descriptor.plannedChorusMix.has_value())
                return "MIX " + percent (*descriptor.plannedChorusMix);
            break;
        case ModifierType::BufferAutoPan:
            if (descriptor.plannedPanDepth.has_value())
                return "DEPTH " + percent (*descriptor.plannedPanDepth);
            break;
        case ModifierType::BufferGranularOn:
        case ModifierType::BufferGranularMomentary:
            if (descriptor.plannedGrainDensityHz.has_value())
                return juce::String (*descriptor.plannedGrainDensityHz, 0) + " G/S";
            break;
        case ModifierType::Reverse:
        case ModifierType::Speed:
        case ModifierType::Stretch:
        case ModifierType::PitchUpOctave:
        case ModifierType::PitchDownOctave:
        case ModifierType::BeatSliceRandom:
        case ModifierType::PingPong:
        case ModifierType::BufferLowPassOn:
        case ModifierType::BufferHighPassOn:
        case ModifierType::BufferVolumeRampDown:
        case ModifierType::BufferTremolo:
        case ModifierType::BufferDuckingOn:
        case ModifierType::BufferSHLowPassOn:
        case ModifierType::BufferSHHighPassOn:
        case ModifierType::MasterHighPassOn:
        case ModifierType::MasterLowPassOn:
        case ModifierType::SwitchPart:
        case ModifierType::QuarterNoteBurst:
        case ModifierType::SwapModifierStack:
        case ModifierType::ResetAll:
        case ModifierType::Unknown:
            break;
    }

    return full (descriptor);
}
} // namespace ModifierVariantFormatter
