# Build and release

[简体中文](BUILD.zh-CN.md)

## Windows x64

Install Visual Studio 2022 Desktop development with C++, a Windows SDK, CMake 3.27+, Git and Python 3.10+. An initial repository build requires network access.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
python scripts/test-vst2-paths.py build/bin/Release/reajsfx.dll
cmake --install build --config Release --prefix stage
python scripts/package.py --stage stage --output release
```

Targets: `reajsfx_vst2`, `modern_reajs_vst3`, `modern_reajs_clap`. The latter two retain internal target and protocol identifiers. Output filenames are lowercase: `reajsfx.dll`, `reajsfx.vst3` and `reajsfx.clap`. The host-visible product name remains `ReaJSFX`.

CMake fetches the exact commits in `dependencies.json`. Wrapper adaptations are applied by `scripts/patch-dependencies.py`. VST3/CLAP runtime changes are generated into the build directory by `cmake/runtime.cmake`. VST2 uses the complete official project source list in a separate build tree, with ABI declarations, Windows resource adaptations, branding changes and the Windows JS root-path fallback fix. It does not link the modern adapters.

The upstream x64 EEL assembly object is used with its assembly source retained. Builds do not require the proprietary Steinberg VST2 SDK. The open interface is documented in `dependencies/vst2-vestige/README.md`.

The Release source ZIP includes populated dependencies and `.reajsfx-revision` markers. CMake uses those trees without fetching them again. The packager reads the archive's `SOURCE_REVISION` when Git metadata is absent. `--revision <commit>` overrides it.

## GitHub Actions

`.github/workflows/build.yml` builds all formats on `windows-2022`, runs the VST2 resource-path tests, and uploads `ReaJSFX-windows-x64`. The tests cover default, relative and absolute resource paths and renamed-DLL INIs, including `Effects` and `Data` reads.

Successful default-branch (`main`) and version-tag builds publish GitHub Releases. The tag `vX.Y.Z` comes from the CMake project version. A missing tag is created at the built commit. Tag pushes must match the project version. Manual runs on the default branch or a version tag follow the same rules. Other branches and pull requests only produce build artifacts.

Releases contain three plugin ZIPs, dependency-complete source, a revision/dependency manifest and SHA-256 checksums. Re-running the same commit and version replaces its assets. A tag pointing to another commit blocks publication and requires a version increment. Existing tags are never moved. Only the release job has `contents: write` permission.
