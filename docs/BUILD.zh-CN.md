# 构建与发布

[English](BUILD.md)

## Windows x64

安装 Visual Studio 2022 的 C++ 桌面开发组件、Windows SDK、CMake 3.27+、Git 和 Python 3.10+。从仓库首次构建需要联网。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
python scripts/test-vst2-paths.py build/bin/Release/reajsfx.dll
cmake --install build --config Release --prefix stage
python scripts/package.py --stage stage --output release
```

构建目标为 `reajsfx_vst2`、`modern_reajs_vst3` 和 `modern_reajs_clap`。后两者保留内部构建目标与协议标识。输出文件名统一小写：`reajsfx.dll`、`reajsfx.vst3` 和 `reajsfx.clap`；宿主显示名称仍为 `ReaJSFX`。

CMake 获取 `dependencies.json` 锁定的提交。`scripts/patch-dependencies.py` 应用包装层适配，`cmake/runtime.cmake` 在构建目录生成 VST3 和 CLAP 的运行时修改。VST2 在独立构建树中采用官方项目的完整源文件清单，补充 ABI 声明、Windows 资源适配、品牌修改及 Windows JS 根目录回退修复，不链接现代格式适配层。

构建使用上游附带的 x64 EEL 汇编对象，同时保留汇编源码。无需专有 Steinberg VST2 SDK。开放接口说明位于 `dependencies/vst2-vestige/README.zh-CN.md`。

Release 源码包包含依赖源码及 `.reajsfx-revision` 标记。CMake 直接使用这些目录，不重复下载。没有 Git 元数据时，打包程序读取源码包中的 `SOURCE_REVISION`。可通过 `--revision <commit>` 覆盖该值。

## GitHub Actions

`.github/workflows/build.yml` 使用 `windows-2022` 配置、构建并整理三种格式。整理前加载实际 VST2 DLL 的临时副本，通过音频输出验证默认 `JS` 目录、相对和绝对 `rootpath`，以及 DLL 改名后的同名 INI，包含脚本和 Data 文件读取。注册表读取仅在测试进程中重定向，不修改注册表键或值。每次成功构建上传 `ReaJSFX-windows-x64` 附件。这些检查不能代替宿主界面或 VST3/CLAP 运行时测试。

发布时更新 `project(ReaJSFX VERSION ...)`，再推送对应的 `vX.Y.Z` 标签。标签与版本不符时会在配置前失败。发布任务仅在版本标签构建成功后运行，`contents: write` 权限仅授予该任务。可选择版本标签手动重试发布。

Release 包含三个格式 ZIP、完整源码 ZIP、记录构建提交和依赖版本的清单，以及 SHA-256 校验文件。重跑同一标签会替换其附件。分支构建和 Pull Request 不发布 Release。
