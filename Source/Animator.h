/*
 ==============================================================================
   Animator.h
   --------------------------------------------------------------------------
   Lightweight animation utility.  Components create Animator instances and
   call tick() from their timer callback.  Each animator drives a 0→1
   progress value over a configurable duration, with easing.

   Supports one-shot and looping modes.  Designed to be composed — a
   component may own several Animators (one per effect).
 ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <functional>

class Animator
{
public:
    enum class Easing { Linear, EaseOut, EaseInOut, Bounce };

    /** Start a one-shot animation from 0→1 over @p durationMs.
        @p onFrame is called every tick with the eased progress [0,1].
        @p onComplete is called once when the animation finishes. */
    void start (int durationMs,
                std::function<void (float progress)> onFrame,
                std::function<void()> onComplete = {},
                Easing easing = Easing::EaseOut)
    {
        durationSec  = (double) durationMs / 1000.0;
        elapsed      = 0.0;
        frameCallback    = std::move (onFrame);
        completeCallback = std::move (onComplete);
        easingMode   = easing;
        running      = true;
        looping      = false;
    }

    /** Start a looping animation (0→1 repeating) at the given cycle period.
        @p onFrame is called every tick with the eased progress [0,1]. */
    void startLoop (int cycleDurationMs,
                    std::function<void (float progress)> onFrame,
                    Easing easing = Easing::Linear)
    {
        durationSec  = (double) cycleDurationMs / 1000.0;
        elapsed      = 0.0;
        frameCallback    = std::move (onFrame);
        completeCallback = {};
        easingMode   = easing;
        running      = true;
        looping      = true;
    }

    /** Advance the animation by @p dtSeconds.  Call this from timerCallback(). */
    void tick (double dtSeconds)
    {
        if (! running)
            return;

        elapsed += dtSeconds;

        if (looping)
        {
            // Wrap around
            while (elapsed >= durationSec)
                elapsed -= durationSec;

            const float raw = (float) (elapsed / durationSec);
            if (frameCallback)
                frameCallback (applyEasing (raw));
        }
        else
        {
            if (elapsed >= durationSec)
            {
                running = false;
                if (frameCallback)
                    frameCallback (1.0f);
                if (completeCallback)
                    completeCallback();
                return;
            }

            const float raw = (float) (elapsed / durationSec);
            if (frameCallback)
                frameCallback (applyEasing (raw));
        }
    }

    void stop()
    {
        running = false;
    }

    bool isRunning() const { return running; }

    /** Get the most recently computed eased progress value (for external queries). */
    float getProgress() const
    {
        if (durationSec <= 0.0)
            return 0.0f;
        const float raw = (float) juce::jlimit (0.0, 1.0, elapsed / durationSec);
        return applyEasing (raw);
    }

private:
    float applyEasing (float t) const
    {
        switch (easingMode)
        {
            case Easing::Linear:
                return t;

            case Easing::EaseOut:
                return 1.0f - (1.0f - t) * (1.0f - t);

            case Easing::EaseInOut:
                return t < 0.5f ? (2.0f * t * t) : (1.0f - std::pow (-2.0f * t + 2.0f, 2.0f) / 2.0f);

            case Easing::Bounce:
            {
                const float n1 = 7.5625f;
                const float d1 = 2.75f;
                float x = t;
                if (x < 1.0f / d1)
                {
                    return n1 * x * x;
                }
                else if (x < 2.0f / d1)
                {
                    x -= 1.5f / d1;
                    return n1 * x * x + 0.75f;
                }
                else if (x < 2.5f / d1)
                {
                    x -= 2.25f / d1;
                    return n1 * x * x + 0.9375f;
                }
                else
                {
                    x -= 2.625f / d1;
                    return n1 * x * x + 0.984375f;
                }
            }
        }
        return t;
    }

    double durationSec  = 0.0;
    double elapsed      = 0.0;
    bool   running      = false;
    bool   looping      = false;
    Easing easingMode   = Easing::EaseOut;

    std::function<void (float)> frameCallback;
    std::function<void()>       completeCallback;
};
