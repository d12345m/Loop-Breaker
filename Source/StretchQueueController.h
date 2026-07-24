#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Tracks SoundTouch input as output-equivalent credit.  Carrying the signed
// fractional remainder between callbacks avoids the positive bias caused by
// independently rounding every host block up to a whole input frame.
class StretchQueueController
{
public:
    void reset (double newOutputPerInputRatio = 1.0) noexcept
    {
        outputCreditFrames = 0.0;
        outputPerInputRatio = sanitiseRatio (newOutputPerInputRatio);
        totalInputFrames = 0;
        totalOutputFrames = 0;
    }

    void setOutputPerInputRatio (double newRatio) noexcept
    {
        outputPerInputRatio = sanitiseRatio (newRatio);
    }

    int getInputFramesRequired (int outputFramesRequired,
                                int targetOutputReserve,
                                int maximumInputFrames) const noexcept
    {
        if (outputFramesRequired <= 0 || maximumInputFrames <= 0)
            return 0;

        const double desiredOutputCredit =
            static_cast<double> (outputFramesRequired + std::max (0, targetOutputReserve));
        const double outputDeficit = desiredOutputCredit - outputCreditFrames;
        if (outputDeficit <= 0.0)
            return 0;

        // The ledger records the small overshoot from ceil(), so it is repaid
        // by a later callback instead of accumulating indefinitely.
        const int inputFrames = static_cast<int> (
            std::ceil (outputDeficit / outputPerInputRatio));
        return std::clamp (inputFrames, 1, maximumInputFrames);
    }

    void recordInputFrames (int frames) noexcept
    {
        if (frames <= 0)
            return;

        outputCreditFrames += static_cast<double> (frames) * outputPerInputRatio;
        totalInputFrames += static_cast<std::uint64_t> (frames);
    }

    void recordOutputFrames (int frames) noexcept
    {
        if (frames <= 0)
            return;

        outputCreditFrames -= static_cast<double> (frames);
        totalOutputFrames += static_cast<std::uint64_t> (frames);
    }

    double getOutputCreditFrames() const noexcept       { return outputCreditFrames; }
    double getOutputPerInputRatio() const noexcept      { return outputPerInputRatio; }
    std::uint64_t getTotalInputFrames() const noexcept  { return totalInputFrames; }
    std::uint64_t getTotalOutputFrames() const noexcept { return totalOutputFrames; }

private:
    static double sanitiseRatio (double ratio) noexcept
    {
        return std::isfinite (ratio) && ratio > 0.0 ? ratio : 1.0;
    }

    double outputCreditFrames = 0.0;
    double outputPerInputRatio = 1.0;
    std::uint64_t totalInputFrames = 0;
    std::uint64_t totalOutputFrames = 0;
};
