# Loop Breaker

Loop Breaker is a JUCE-based audio instrument for transforming and performing with audio buffers. It supports variable-speed playback, time stretching, slicing, MIDI control, and VST3/AU/AUv3/CLAP plug-in targets.

## Build

Requirements: CMake 3.22 or later, a C++17 compiler, and a local JUCE checkout.

```sh
git clone --recurse-submodules https://github.com/d12345m/BufferTest.git
cd BufferTest
cmake -B build -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

The CMake project creates platform-appropriate plug-in targets. To validate the VST3 build with pluginval:

```sh
cmake --build build --config Release --target validate
```

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

This repository does not yet include a license. Add an explicit license file before accepting outside contributions or redistribution.
