# ReaJSFX

[简体中文](README.zh-CN.md)

The standalone ReaJS plugin has gone roughly a decade without an update, while JSFX has continued to evolve. ReaJSFX is an update effort based on the official open-source code, preserving ReaJS's script-based workflow while extending support for current JSFX and modern plugin formats. It provides Windows x64 builds in **VST2, VST3 and CLAP** formats.

VST2 builds the official [Justin Frankel JSFX](https://github.com/justinfrankel/jsfx) host with build adaptations, display branding and a Windows resource-path correction. It is not a Cockos-issued binary. VST3 and CLAP use the same upstream engine with this project's format adapters and runtime corrections.

## Install

Download a format package from [Releases](https://github.com/zaibuyidao/ReaJSFX/releases). Close the DAW before replacing an existing installation.

| Format | Install |
|---|---|
| VST2 | Copy `VST2/reajsfx.dll` into the DAW's VST2 directory. |
| VST3 | Copy the entire `VST3/reajsfx.vst3` directory into `C:\Program Files\Common Files\VST3`. |
| CLAP | Copy `CLAP/reajsfx.clap` into `C:\Program Files\Common Files\CLAP`. |

Rescan plugins. Plugin IDs are retained from the previous implementation, so remove duplicate installations when upgrading.

## Use

Place scripts and imports under `Effects`, with associated assets under the sibling `Data` directory. Click **Load** to select a script. **Edit** opens the upstream editor. The **+** button appears for scripts declaring configuration options.

Copy [ReaJSFX.ini.example](docs/ReaJSFX.ini.example) beside the plugin as `reajsfx.ini` to specify `rootpath` or an instance variant. For VST3, the INI can also sit beside the outer bundle. Set `rootpath=JS` in `[ReaJS]` for a library in `JS/Effects` and `JS/Data` relative to the INI directory. See [resource lookup](docs/COMPATIBILITY.md#resource-lookup) for automatic discovery and renamed plugins.

Save the DAW project to retain plugin state. Scripts and samples remain external. VST2 state is not imported by VST3 or CLAP. See [format differences](docs/COMPATIBILITY.md).

## Build and release

Requires Visual Studio 2022 C++ tools, a Windows SDK, CMake 3.27+, Git and Python 3.10+.

```powershell
.\build.ps1 -Package
```

This builds all three formats and writes packages to `release/`. Dependencies are pinned in [dependencies.json](dependencies.json) and fetched into the ignored `upstream/` directory. No system plugin directories are modified.

GitHub Actions builds pushes, pull requests and manual runs. Successful builds on the default branch or a matching `vX.Y.Z` tag publish GitHub Releases. Each new release commit requires an unused CMake version. See [build and release rules](docs/BUILD.md#github-actions).

## License

Project adapters and the JSFX engine use LGPL-2.1-or-later. The VST2 interface declarations additionally use GPL-2.0-or-later. Third-party notices and original copyright statements are retained in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt). Release source archives include the pinned dependencies and build scripts.
