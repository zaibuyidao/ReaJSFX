# Build and release

[简体中文](BUILD.zh-CN.md)

## Windows x64

Install Visual Studio 2022 Desktop development with C++, a Windows SDK, CMake 3.27+, Git and Python 3.10+. An initial repository build requires network access.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
cmake --install build --config Release --prefix stage
python scripts/package.py --stage stage --output release
```

Targets: `reajsfx_vst2`, `modern_reajs_vst3`, `modern_reajs_clap`. The latter two retain internal target and protocol identifiers. All output filenames and host-visible product names are `ReaJSFX`.

CMake fetches the exact commits in `dependencies.json`. Wrapper adaptations are applied by `scripts/patch-dependencies.py`. VST3/CLAP runtime changes are generated into the build directory by `cmake/runtime.cmake`. VST2 uses the complete official project source list in a separate build tree, with ABI declarations, Windows resource adaptations and branding changes. It does not link the modern adapters.

The upstream x64 EEL assembly object is used with its assembly source retained. Builds do not require the proprietary Steinberg VST2 SDK. The open interface is documented in `dependencies/vst2-vestige/README.md`.

The Release source ZIP includes populated dependencies and `.reajsfx-revision` markers. CMake uses those trees without fetching them again. The packager reads the archive's `SOURCE_REVISION` when Git metadata is absent. `--revision <commit>` overrides it.

## GitHub Actions

`.github/workflows/build.yml` uses `windows-2022` to configure, build and stage all formats. Every successful build uploads the `ReaJSFX-windows-x64` artifact. No runtime tests are executed by this workflow.

To release, update `project(ReaJSFX VERSION ...)` and push the corresponding `vX.Y.Z` tag. A tag mismatch fails before configuration. The release job runs only for version tags after a successful build, with `contents: write` scoped to that job. Manual execution against a version tag can retry publication.

Assets comprise three format ZIPs, a complete source ZIP, a manifest containing the build revision and dependency commits, and SHA-256 checksums. Re-running the same tag replaces its assets. Branch builds and pull requests do not publish releases.
