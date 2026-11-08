# VST2 接口

[English](README.md)

`vestige.h` 来自 Carla 提交 `97a9e0740baf6df2df942495c02532a624c44682` 的 `source/includes/vestige/vestige.h`，采用 GPL-2.0-or-later。原始声明及 GPL 全文均已保留。

`scripts/prepare-vst2.py` 补充官方 JSFX 适配层需要的公开事件字段、完整 AEffect 尾部、SysEx 与引脚结构及常量，并通过编译期断言约束 Windows x64 布局。这些声明不实现 DSP 或宿主行为。VST3 和 CLAP 不编译此依赖。

来源：https://github.com/falkTX/Carla/blob/97a9e0740baf6df2df942495c02532a624c44682/source/includes/vestige/vestige.h
