# Loop Breaker

Loop Breaker is a JUCE-based audio instrument for transforming and performing with audio buffers. It supports variable-speed playback, time stretching, slicing, MIDI control, and VST3/AU/AUv3/CLAP plug-in targets.

## How it works

Loop Breaker works with up to eight audio buffers (pads). During playback, it periodically places a modifier in a queue. You can assign that modifier to a pad, or let Loop Breaker select an eligible pad automatically. Modifiers accumulate, allowing each loop to develop independently over time.

Modifiers can change playback behavior or add processing. Playback modifiers include speed changes, reverse playback, time stretching, re-pitching, loop-part changes, and slice shuffling, arpeggiation, and repetition. Processing modifiers include reverb, delay, chorus, filters, granular effects, auto-pan, and tremolo. Parameters and onset timing are chosen as modifiers are applied.

The probability system controls both the chance that each modifier type appears and the chance that a pad is selected automatically. These probability controls are exposed as automatable parameters and can be mapped to MIDI CC messages, so they can be changed from a controller, DAW automation, or a modulation source.

Modifier-stack presets can be saved and recalled between DAW sessions, with MIDI note mappings available for preset recall. The plug-in also provides a main stereo mix plus eight optional stereo output buses, allowing each pad to be recorded, edited, and mixed separately in a DAW.

## Build

Requirements: CMake 3.22 or later, a C++17 compiler, and a local JUCE checkout.

```sh
git clone --recurse-submodules https://github.com/d12345m/Loop-Breaker.git
cd Loop-Breaker
cmake -B build -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

The CMake project creates platform-appropriate plug-in targets. To validate the VST3 build with pluginval:

```sh
cmake --build build --config Release --target validate
```

## Release credentials

The macOS release workflow needs signing, notarization, and Netlify credentials. Their names and non-sensitive setup notes are in [`.env.example`](.env.example); GitHub Actions reads the actual values from the `Production` environment's secrets. Copy the example to a local `.env` only when running release tooling locally, and never commit the populated file, certificates, or keychains.

## Documentation

The [documentation index](docs/README.md) links to the user guide, DSP architecture, technical reports, implementation plans, and historical project notes.

## Repository layout

- `Source/` — application and audio-processing code
- `Resources/` — bundled fonts and other binary resources
- `ThirdParty/` and `Submodules/` — external dependencies
- `Installer/` — macOS packaging scripts and resources
- `website/` — static project website
- `docs/` — project documentation

## License

Copyright (c) 2026 Glow Machine Audio.

Loop Breaker is licensed under the [GNU General Public License, version 3](LICENSE) (`GPL-3.0-only`). The repository includes third-party submodules and bundled fonts under their own licenses; retain their included license and attribution files when redistributing builds.
