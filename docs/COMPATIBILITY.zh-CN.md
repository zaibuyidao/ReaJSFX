# 格式兼容性

[English](COMPATIBILITY.md)

| 能力 | 官方源码 VST2 | VST3 / CLAP |
|---|---|---|
| JSFX 编译器、滑块、图形、编辑器、导入 | 上游实现 | 复用上游实现 |
| 音频接口精度 | float32 | float32 与 float64 |
| MIDI 缓冲 API | 适配层缺少 `midisend_buf` 和 `midirecv_buf` 回调 | 已实现 |
| FLAC 资源 | 独立适配层未绑定解码器 | 使用 REAPER 媒体服务或流式解码回退 |
| 状态 | 官方 VST2 chunk | 独立的现代格式状态、高精度参数与资源根目录迁移 |
| REAPER 路由与媒体插入 | 官方适配层行为 | 绑定所属 REAPER 效果和工程，包含 Take FX 引脚映射 |

VST2 保留上游运行时行为及限制，包括初始化和 `midisyx` 返回值问题。VST3 和 CLAP 应用对应修正。已弃用的经典链式 API 不重新实现。

## 资源路径

`rootpath` 指定资源根目录。脚本位于 `Effects`，关联资源位于 `Data`。非空的 `[ReaJS] rootpath` 覆盖自动查找，支持绝对路径、UNC 路径、盘符根目录相对路径及普通相对路径。

VST2 读取 DLL 同名 INI，相对路径以 DLL 所在目录为基准。未设置 `rootpath` 时，使用 HKLM `Software\REAPER` 的默认值。如果该值非空，且 shell AppData 目录下存在 `REAPER\reaper-install-rev.txt`，则改用该 AppData 下的 `REAPER` 目录。没有配置 REAPER 路径时，回退到 `<DLL 所在目录>\JS`。

VST3/CLAP 先在模块目录及其祖先目录中查找 `Effects`，然后依次检查 REAPER 注册表路径（HKCU 优先于 HKLM，各自先查 64 位视图再查 32 位视图）和 `%APPDATA%\REAPER`。均不存在 `Effects` 时，回退到 `<模块目录>\JS`。配置优先读取模块同名 INI，其次读取 `ReaJSFX.ini`。VST3 在模块旁没有 INI 时也会检查外层 bundle 旁的配置。相对 `rootpath` 以选中的 INI 所在目录为基准。

## 宿主配色

VST3/CLAP 的 Load 行在宿主提供 `GSC_mainwnd` 时使用 REAPER 配色。编辑器打开期间响应 Windows 主题消息，并每 250 ms 检查配色变化。没有该接口时使用 Windows 控件。VST2 使用上游编辑器的主题处理。

## 运行时限制

REAPER 工程服务依赖 REAPER，无法在其他 DAW 中提供。标准 VST3 不能表达所有系统实时 MIDI 事件。未实现原生 MIDI 2.0/MPE 扩展。每块 MIDI 保护上限为 1,048,576 个事件和 64 MiB，超限或宿主拒收会报告错误。

脚本和资源保持外置，状态不嵌入这些文件，也不将 VST2 预置迁移到现代格式。宿主初始化历史、自由运行相位、状态精度和脚本运算可能造成数值差异。不保证全部 JSFX 与 DAW 的位级一致或穷尽兼容性。
