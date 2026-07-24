# CMake Warning Cleanup

Last updated: 2026-07-23

## Current status

The CMake configure, iOS Debug app build, desktop Release plug-in build, and
Release unit-test build are warning-free.

| Check | Result |
| --- | --- |
| iOS CMake configure | Succeeded, 0 warnings |
| iOS Debug unsigned build | Succeeded, 0 compiler warnings |
| Desktop Release `LoopBreaker_All` build | Succeeded, 0 compiler warnings |
| Desktop Release `LoopBreaker_CLAP` build | Succeeded, 0 compiler warnings |
| Release `LoopBreakerTests` build | Succeeded, 0 compiler warnings |
| CTest | 1/1 test passed |

Xcode still prints informational notes for CMake script phases that intentionally
run on every build. They are not warnings. Desktop configuration may also report
that optional JACK and AAX SDK support is unavailable; neither is required by
Loop Breaker.

## Baseline

The initial clean unsigned iOS Debug build emitted 147 compiler warning lines.
After deduplicating headers included by multiple translation units, there were
108 distinct warning locations:

- 54 in Loop Breaker source.
- 54 in vendored SoundTouch source.

The first desktop Release build exposed eight additional Release-only warnings:
one unused lambda capture, three unused debug constants, and four unused debug
fields. The unit-test target exposed two more warning patterns: a generated JUCE
macro collision and one double-to-float conversion.

### Distinct iOS warnings by diagnostic

| Diagnostic | Total | First-party | Vendored |
| --- | ---: | ---: | ---: |
| `-Wsign-conversion` | 65 | 21 | 44 |
| `-Wunused-parameter` | 9 | 9 | 0 |
| `-Wswitch-enum` | 7 | 7 | 0 |
| `-Wfloat-equal` | 7 | 6 | 1 |
| `-Wmissing-prototypes` | 3 | 0 | 3 |
| `-Wextra-semi` | 3 | 0 | 3 |
| `-Wshorten-64-to-32` | 2 | 2 | 0 |
| `-Wshadow-field` | 2 | 1 | 1 |
| `-Wshadow` | 2 | 0 | 2 |
| `-Winconsistent-missing-destructor-override` | 2 | 2 | 0 |
| `-Wdeprecated-declarations` | 2 | 2 | 0 |
| `-Wunused-lambda-capture` | 1 | 1 | 0 |
| `-Wshadow-field-in-constructor` | 1 | 1 | 0 |
| `-Wmissing-field-initializers` | 1 | 1 | 0 |
| `-Wfloat-conversion` | 1 | 1 | 0 |

### First-party warning locations

| Source | Distinct locations | Warning families |
| --- | ---: | --- |
| `Source/PluginProcessor.cpp` | 11 | index conversions, float comparison, deprecated `MemoryBlock` API |
| `Source/AudioBufferManager.cpp` | 8 | signed array indexes |
| `Source/PluginEditor.cpp` | 7 | signed indexes, enum handling, float comparison, unused capture |
| `Source/AudioBuffer.h` | 6 | unnamed default listener parameters |
| `Source/AudioBuffer.cpp` | 5 | field shadowing, enum handling, float comparisons, unused parameter |
| `Source/ThemeLookAndFeel.h` | 3 | float comparison and unused parameters |
| `Source/TearingDebugPanel.h` | 3 | deprecated font API and index conversions |
| `Source/NodeClipDetector.h` | 2 | unhandled sentinel enum value |
| `Source/ModifierHistoryPanel.h` | 2 | float-to-int conversion and partial enum switch |
| `Source/AppState.h` | 1 | unhandled modifier enum values |
| `Source/AudioBufferManager.h` | 1 | missing destructor `override` |
| `Source/ChannelStrip.h` | 1 | exact sample-rate comparison |
| `Source/Modifier.cpp` | 1 | partial enum switch |
| `Source/ModifierRegistry.h` | 1 | partial aggregate initialization |
| `Source/ModifierScheduler.h` | 1 | missing destructor `override` |
| `Source/PluginEditor.h` | 1 | inherited-member shadowing |

### Vendored SoundTouch warning locations

| Source | Distinct locations |
| --- | ---: |
| `BPMDetect.cpp` | 14 |
| `FIRFilter.cpp` | 13 |
| `SoundTouch.cpp` | 8 |
| `RateTransposer.cpp` | 7 |
| `TDStretch.cpp` | 6 |
| `AAFilter.cpp` | 2 |
| `TDStretch.h` | 2 |
| `FIFOSampleBuffer.h` | 1 |
| `RateTransposer.h` | 1 |

These were primarily signedness conversions, plus a few missing prototypes,
shadowed names, extra semicolons, and one exact float comparison.

## Remediation

- SoundTouch now builds as its own CMake library instead of being unity-included
  into `PluginProcessor.cpp`. Its public headers are treated as system headers,
  so project warning policy stays strict without applying it to vendored code.
- Integer indexes now use the container's native index type or an explicit,
  bounds-checked conversion.
- Enum switches are exhaustive where every value is meaningful. UI-only partial
  mappings use conditionals so a deliberate fallback is clear.
- Exact floating-point comparisons were replaced with approximate comparisons
  or sign comparisons as appropriate.
- Deprecated JUCE `Font` and `MemoryBlock` APIs were replaced.
- Virtual destructors now state `override`; unused callback parameters are
  unnamed; shadowed members and constructor parameters were renamed.
- Debug-only constants and state are compiled only in Debug builds.
- The unit-test console target links the shared-code archive without inheriting
  plug-in-only compile definitions, preventing JUCE macro collisions.

## Reproduction commands

```sh
cmake -S . -B build-ios -G Xcode \
  -DJUCE_DIR=/path/to/JUCE \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DLOOP_BREAKER_BUILD_TESTS=OFF

xcodebuild -project build-ios/LoopBreaker.xcodeproj \
  -scheme LoopBreaker_Standalone \
  -configuration Debug \
  -destination 'generic/platform=iOS' \
  CODE_SIGNING_ALLOWED=NO clean build

cmake -S . -B build-desktop-check \
  -DJUCE_DIR=/path/to/JUCE \
  -DLOOP_BREAKER_BUILD_TESTS=OFF
cmake --build build-desktop-check --config Release \
  --target LoopBreaker_All --clean-first
cmake --build build-desktop-check --config Release \
  --target LoopBreaker_CLAP

cmake -S . -B build \
  -DJUCE_DIR=/path/to/JUCE \
  -DLOOP_BREAKER_BUILD_TESTS=ON
cmake --build build --config Release --target LoopBreakerTests
ctest --test-dir build -C Release --output-on-failure
```
