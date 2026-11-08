# Format compatibility

[简体中文](COMPATIBILITY.zh-CN.md)

| Capability | Official-source VST2 | VST3 / CLAP |
|---|---|---|
| JSFX compiler, sliders, graphics, editor, imports | Upstream implementation | Shared upstream implementation |
| Audio boundary | float32 | float32 and float64 |
| MIDI buffer APIs | Missing frame callbacks for `midisend_buf` and `midirecv_buf` | Implemented |
| FLAC assets | Decoder binding absent in the standalone frame | REAPER media services or streaming fallback |
| State | Official VST2 chunk | Native modern state, high-precision values and resource-root relocation |
| REAPER routing and media insertion | Official frame behavior | Bound to the owning REAPER FX and project, including Take FX pin mapping |

VST2 retains upstream runtime behavior and limitations, including initialization and `midisyx` return-value issues. VST3/CLAP apply the corresponding corrections. Upstream deprecated classic-chain APIs are not reconstructed.

Version 0.1.1 restores the Windows VST2 resource fallback missing from the pinned upstream source: after constructing `<DLL directory>\JS`, save it as `g_root_path`. `Effects` and `Data` are children of that root, not of the DLL directory. The original precedence is retained: the HKLM `Software\REAPER` default value, optionally replaced by the shell AppData `REAPER` directory when `reaper-install-rev.txt` exists; otherwise the adjacent `JS` directory. A nonempty `[ReaJS] rootpath` in the DLL-basename INI overrides discovery (absolute/UNC, drive-root-relative, or DLL-relative). Renaming the DLL changes its INI basename.

This was checked against the installed legacy `reajs.dll.old`, SHA-256 `600223acd0337c494a0bc2fb2a399f5ba477c6c0886e9c1e2c1f7475810fcf72`, using MSVC dumpbin static disassembly. At VA `0x18009B637` it references `Software\REAPER`; `0x18009B88C` appends `JS`, and `0x18009B8C0`–`0x18009B8CC` stores the resulting path into the same root string used by the registry branch. The INI override starts at `0x18009BB98`. These are static observations, not a claim of exhaustive legacy reverse engineering.

The VST3/CLAP Load row uses REAPER's `GSC_mainwnd` colors when available, and refreshes when the palette changes while the editor is open. Other hosts retain Windows controls. VST2's existing native theme handling is unchanged.

Runtime probes confirm the legacy DLL's adjacent `JS` fallback works, the unpatched new DLL's fallback fails, and 0.1.1 passes default, relative, `\.\`-style relative, absolute and renamed-DLL INI cases, including `Data` file reads. For legacy INIs use `rootpath=.\custom-content`: the old DLL failed the equivalent bare `rootpath=custom-content` probe; 0.1.1 accepts both spellings.

REAPER project services require REAPER and are unavailable in unrelated DAWs. Standard VST3 cannot represent every system realtime MIDI event. Native MIDI 2.0/MPE extensions are not implemented. MIDI protection limits are 1,048,576 events and 64 MiB per block. Exceeding these limits or a host rejecting output is reported.

Scripts and assets are external. State does not embed them or migrate VST2 presets to modern formats. Host initialization history, free-running phases, state precision and script arithmetic can produce numerical differences. This is not a guarantee of bit-identical output or exhaustive compatibility with every JSFX and DAW.
