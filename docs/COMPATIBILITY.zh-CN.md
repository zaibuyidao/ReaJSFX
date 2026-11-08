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

0.1.1 修复了锁定上游源码中 Windows VST2 的路径回退遗漏：拼出 `<DLL 所在目录>\JS` 后，将它保存为 `g_root_path`。`Effects` 和 `Data` 位于此根目录之下。保留旧版查找顺序：首先读取 HKLM `Software\REAPER` 默认值；如果 shell AppData 下存在 `REAPER\reaper-install-rev.txt`，改用该 AppData REAPER 目录；未找到 REAPER 时回退到 DLL 相邻的 `JS`。DLL 同名 INI 的 `[ReaJS] rootpath` 非空时覆盖自动查找，支持绝对/UNC、当前盘根相对和 DLL 相对路径。DLL 改名后需使用对应名称的 INI。

已用 MSVC dumpbin 静态反汇编核对本机旧版 `reajs.dll.old`，SHA-256 为 `600223acd0337c494a0bc2fb2a399f5ba477c6c0886e9c1e2c1f7475810fcf72`。地址 `0x18009B637` 引用 REAPER 注册表路径，`0x18009B88C` 拼入 `JS`，`0x18009B8C0`–`0x18009B8CC` 将结果写入与注册表分支相同的根目录字符串；`0x18009BB98` 开始读取 INI 覆盖。这是路径相关的静态核对，不代表对旧 DLL 的全部行为完成逆向验证。

VST3/CLAP 的 Load 行在宿主提供接口时采用 REAPER 的 `GSC_mainwnd` 颜色，并在编辑器打开期间随配色变化刷新。其他宿主保留 Windows 控件；VST2 原有主题处理不变。

实际 DLL 音频测试确认：旧版的相邻 `JS` 回退正常，未修新版失败；0.1.1 的默认、相对、带 `.\` 的相对、绝对及 DLL 改名 INI 共五项均通过，同时验证 `Data` 文件读取。旧版 INI 建议写 `rootpath=.\custom-content`：旧 DLL 在裸相对写法 `rootpath=custom-content` 的测试中失败，而 0.1.1 两种写法都通过。

REAPER 工程服务依赖 REAPER，无法在其他 DAW 中提供。标准 VST3 不能表达所有系统实时 MIDI 事件。未实现原生 MIDI 2.0/MPE 扩展。每块 MIDI 保护上限为 1,048,576 个事件和 64 MiB，超限或宿主拒收会报告错误。

脚本和资源保持外置，状态不嵌入这些文件，也不将 VST2 预置迁移到现代格式。宿主初始化历史、自由运行相位、状态精度和脚本运算可能造成数值差异。不保证全部 JSFX 与 DAW 的位级一致或穷尽兼容性。
