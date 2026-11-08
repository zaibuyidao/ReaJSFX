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

构建目标为 `reajsfx_vst2`、`modern_reajs_vst3` 和 `modern_reajs_clap`。后两者保留内部构建目标与协议标识。输出文件名统一小写：`reajsfx.dll`、`reajsfx.vst3` 和 `reajsfx.clap`。宿主显示名称仍为 `ReaJSFX`。

CMake 获取 `dependencies.json` 锁定的提交。`scripts/patch-dependencies.py` 应用包装层适配，`cmake/runtime.cmake` 在构建目录生成 VST3 和 CLAP 的运行时修改。VST2 在独立构建树中采用官方项目的完整源文件清单，补充 ABI 声明、Windows 资源适配、品牌修改及 Windows JS 根目录回退修复，不链接现代格式适配层。

构建使用上游附带的 x64 EEL 汇编对象，同时保留汇编源码。无需专有 Steinberg VST2 SDK。开放接口说明位于 `dependencies/vst2-vestige/README.zh-CN.md`。

Release 源码包包含依赖源码及 `.reajsfx-revision` 标记。CMake 直接使用这些目录，不重复下载。没有 Git 元数据时，打包程序读取源码包中的 `SOURCE_REVISION`。可通过 `--revision <commit>` 覆盖该值。

## GitHub Actions

`.github/workflows/build.yml` 在 `windows-2022` 上构建全部格式，执行 VST2 资源路径测试，并上传 `ReaJSFX-windows-x64`。测试覆盖默认、相对和绝对资源路径及 DLL 改名后的同名 INI，包含 `Effects` 和 `Data` 读取。

默认分支（`main`）及版本标签构建成功后发布 GitHub Release。`vX.Y.Z` 取自 CMake 项目版本，缺少的标签会在本次构建的提交上创建。推送的版本标签必须与项目版本一致。默认分支或版本标签的手动运行遵循相同规则。其他分支和 Pull Request 只生成构建附件。

Release 包含三个插件 ZIP、含依赖的完整源码、提交及依赖清单和 SHA-256 校验文件。重跑同一提交和版本会替换附件。标签指向其他提交时拒绝发布，需要递增版本。已有标签不会移动。仅发布任务具有 `contents: write` 权限。
