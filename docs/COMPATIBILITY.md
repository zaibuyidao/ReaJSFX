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

REAPER project services require REAPER and are unavailable in unrelated DAWs. Standard VST3 cannot represent every system realtime MIDI event. Native MIDI 2.0/MPE extensions are not implemented. MIDI protection limits are 1,048,576 events and 64 MiB per block. Exceeding these limits or a host rejecting output is reported.

Scripts and assets are external. State does not embed them or migrate VST2 presets to modern formats. Host initialization history, free-running phases, state precision and script arithmetic can produce numerical differences. This is not a guarantee of bit-identical output or exhaustive compatibility with every JSFX and DAW.
