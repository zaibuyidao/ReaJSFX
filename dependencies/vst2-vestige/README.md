# VST2 interface

[简体中文](README.zh-CN.md)

`vestige.h` is Carla's GPL-2.0-or-later interface declaration from commit `97a9e0740baf6df2df942495c02532a624c44682`, path `source/includes/vestige/vestige.h`. Its original notices and GPL text are retained.

`scripts/prepare-vst2.py` adds the public event fields, complete AEffect tail, SysEx/pin records and constants required by the official JSFX frame. Compile-time assertions constrain Windows x64 layouts. These declarations implement no DSP or host behavior. VST3 and CLAP do not compile this dependency.

Source: https://github.com/falkTX/Carla/blob/97a9e0740baf6df2df942495c02532a624c44682/source/includes/vestige/vestige.h
