# ReaJSFX

[English](README.md)

独立 ReaJS 插件已约十年未更新，而 JSFX 仍在持续发展。ReaJSFX 是基于官方开源代码的一次更新尝试，延续 ReaJS 加载和运行脚本的使用方式，并补充较新 JSFX 与现代插件格式的支持。当前提供 Windows x64 的 **VST2、VST3 和 CLAP** 版本。

VST2 基于 [Justin Frankel 的官方 JSFX 源码](https://github.com/justinfrankel/jsfx)构建，仅应用构建适配和显示名称修改，不是 Cockos 发布的二进制。VST3 和 CLAP 复用同一上游引擎，并包含本项目的格式适配与运行时修正。

## 安装

从 [Releases](https://github.com/zaibuyidao/ReaJSFX/releases) 下载所需格式。更新前关闭 DAW。

| 格式 | 安装方式 |
|---|---|
| VST2 | 将 `VST2/reajsfx.dll` 复制到 DAW 的 VST2 插件目录。 |
| VST3 | 将整个 `VST3/reajsfx.vst3` 目录复制到 `C:\Program Files\Common Files\VST3`。 |
| CLAP | 将 `CLAP/reajsfx.clap` 复制到 `C:\Program Files\Common Files\CLAP`。 |

重新扫描插件。插件 ID 沿用原实现，更新时请移除重复安装。

## 使用

将脚本及导入文件放入 `Effects`，相关资源放入同级 `Data` 目录。点击 **Load** 选择脚本，点击 **Edit** 打开上游编辑器。声明了配置项的脚本会显示 **+** 按钮。

需要指定 `rootpath` 或实例变体时，将 [ReaJSFX.ini.example](docs/ReaJSFX.ini.example) 复制到插件旁并命名为 `ReaJSFX.ini`。VST3 也支持将配置文件放在外层 bundle 旁。配置节名称保留为 `[ReaJS]`。

保存 DAW 工程即可保存插件状态。脚本和样本保持外置。VST3 和 CLAP 不导入 VST2 状态。具体范围见[格式差异](docs/COMPATIBILITY.zh-CN.md)。

## 构建与发布

需要 Visual Studio 2022 C++ 工具、Windows SDK、CMake 3.27+、Git 和 Python 3.10+。

```powershell
.\build.ps1 -Package
```

命令构建三种格式，并将发布包写入 `release/`。依赖提交锁定在 [dependencies.json](dependencies.json)，配置时下载到被 Git 忽略的 `upstream/`。构建不会修改系统插件目录。

GitHub Actions 在推送、Pull Request 和手动触发时执行构建。推送与 CMake 版本一致的 `vX.Y.Z` 标签，会自动发布三个插件包、包含依赖的完整源码包、构建清单及 SHA-256 校验文件。详见[构建说明](docs/BUILD.zh-CN.md)。

## 许可证

项目适配代码与 JSFX 引擎采用 LGPL-2.1-or-later。VST2 接口声明另采用 GPL-2.0-or-later。[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) 保留第三方声明与原始版权信息。Release 源码包包含锁定的依赖源码与构建脚本。
