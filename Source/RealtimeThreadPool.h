/*
  ==============================================================================

    RealtimeThreadPool.h

    §9.1  Lightweight thread pool for parallel real-time audio processing.

    Pre-spawns worker threads that use atomic work-stealing to process
    independent tasks in parallel.  The calling thread also participates,
    so with N workers the total parallelism is N+1.

    Design constraints for real-time audio:
    - Zero heap allocation on the dispatch path
    - No mutex / condition_variable on the calling (audio) thread
    - Workers use a spin-yield loop with sleep fallback when idle

  ==============================================================================
*/

#pragma once

#include <atomic>
#include <thread>
#include <array>
#include <algorithm>
#include <chrono>
#include <type_traits>

#if defined(__APPLE__)
  #include <pthread.h>
#endif

class RealtimeThreadPool
{
public:
    static constexpr int kMaxWorkers = 7;

    /** Create a pool with the given number of background worker threads.
        The calling thread of processAll() also participates, so total
        parallelism is numWorkers + 1. */
    explicit RealtimeThreadPool (int numWorkers = 3)
        : activeWorkers (std::min (std::max (numWorkers, 0), kMaxWorkers))
    {
        for (int i = 0; i < activeWorkers; ++i)
            workers[(size_t) i] = std::thread (&RealtimeThreadPool::workerLoop, this, i);
    }

    ~RealtimeThreadPool()
    {
        shouldExit.store (true, std::memory_order_release);
        generation.fetch_add (1, std::memory_order_release);   // wake sleeping workers

        for (int i = 0; i < activeWorkers; ++i)
            if (workers[(size_t) i].joinable())
                workers[(size_t) i].join();
    }

    RealtimeThreadPool (const RealtimeThreadPool&) = delete;
    RealtimeThreadPool& operator= (const RealtimeThreadPool&) = delete;

    //==========================================================================
    /** Dispatch numTasks independent work items and block until all complete.

        taskFunc(int taskIndex) is called for each task, potentially from
        multiple threads simultaneously.  The calling thread also participates.

        This call is zero-allocation and safe for the real-time audio thread.
        taskFunc must be safe to call concurrently for different taskIndex values. */
    template <typename F>
    void processAll (int numTasks, F&& taskFunc)
    {
        if (numTasks <= 0)
            return;

        // Fast path: single task or no workers — run inline on calling thread
        if (numTasks == 1 || activeWorkers == 0)
        {
            for (int i = 0; i < numTasks; ++i)
                taskFunc (i);
            return;
        }

        // Zero-alloc type erasure: store a static trampoline + void* to
        // the caller's stack-allocated lambda.
        struct Trampoline
        {
            static void call (int idx, void* ctx)
            {
                (*static_cast<std::remove_reference_t<F>*> (ctx)) (idx);
            }
        };

        dispatchedFunc    = &Trampoline::call;
        dispatchedContext  = const_cast<void*> (static_cast<const void*> (&taskFunc));
        totalTasks        = numTasks;
        nextTask.store (0, std::memory_order_relaxed);
        completedTasks.store (0, std::memory_order_relaxed);

        // Release-fence ensures workers see the new dispatch state before the
        // generation bump wakes them.
        generation.fetch_add (1, std::memory_order_release);

        // Calling thread also grabs and runs tasks
        runTasks();

        // Spin-wait until all tasks are done.  In practice this is very brief
        // because the calling thread itself processed ~1/N of the work.
        while (completedTasks.load (std::memory_order_acquire) < numTasks)
        {
            // intentional spin
        }
    }

    int getNumWorkers() const noexcept { return activeWorkers; }

private:
    //==========================================================================
    void runTasks()
    {
        for (;;)
        {
            const int idx = nextTask.fetch_add (1, std::memory_order_relaxed);
            if (idx >= totalTasks)
                break;

            dispatchedFunc (idx, dispatchedContext);
            completedTasks.fetch_add (1, std::memory_order_release);
        }
    }

    void workerLoop (int /*workerIndex*/)
    {
      #if defined(__APPLE__)
        pthread_setname_np ("RT Audio Worker");
      #endif

        int lastGen = generation.load (std::memory_order_relaxed);
        int idleSpins = 0;
        static constexpr int kSpinBeforeSleep = 2000;

        while (! shouldExit.load (std::memory_order_relaxed))
        {
            const int gen = generation.load (std::memory_order_acquire);

            if (gen != lastGen)
            {
                lastGen = gen;
                idleSpins = 0;

                if (! shouldExit.load (std::memory_order_relaxed))
                    runTasks();
            }
            else if (idleSpins < kSpinBeforeSleep)
            {
                ++idleSpins;
                std::this_thread::yield();
            }
            else
            {
                // Extended idle — sleep briefly to conserve CPU (~100µs).
                // Worst-case wake latency is well within a 128-sample block budget.
                std::this_thread::sleep_for (std::chrono::microseconds (100));
            }
        }
    }

    //==========================================================================
    std::array<std::thread, kMaxWorkers> workers {};
    int activeWorkers = 0;

    // Dispatch state (written by processAll caller, read by workers)
    void (*dispatchedFunc)(int, void*) = nullptr;
    void* dispatchedContext = nullptr;
    int totalTasks = 0;

    // Lock-free work distribution
    std::atomic<int>  nextTask       { 0 };
    std::atomic<int>  completedTasks { 0 };
    std::atomic<int>  generation     { 0 };
    std::atomic<bool> shouldExit     { false };
};
