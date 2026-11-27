<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 David Behnke -->

# Crucible

[English](README.md) · **简体中文**

**KF8S 为五款 DMR 电台制作的实验性固件。** Crucible 是 [RadioFoundry](https://github.com/radiofoundry/radiofoundry)
的下游版本：同一套 MIT 许可的核心，只保留我们使用的电台，并按我们的偏好调整。

> [!IMPORTANT]
> **Crucible 不是 RadioFoundry。** RadioFoundry 项目不制作、不审核、也不支持 Crucible。运行 Crucible 的电台出了问题，
> 请不要找 RadioFoundry，请找我们：[获取帮助](#获取帮助)。

## Crucible 是什么

- **子集。** 只支持下面列出的五款电台。RadioFoundry 支持的其他电台在这里不构建、不测试。
- **有主见。** 开启我们需要的功能，部分默认设置不同，改动先在我们的电台上运行。差异见[更新日志](docs/kf8sfork/CHANGELOG.md)。
- **下游。** 我们定期合并 RadioFoundry 的主分支。每个版本都注明包含的 RadioFoundry 提交。
- **实验性。** 版本只在我们的电台上测试过，没有覆盖所有硬件版本。请保留备份。

## 电台

| 电台 |
|---|
| Baofeng DM-32UV |
| Baofeng DM-1701、Retevis RT84 |
| Radioddity DB25-D PRO、Kydera CDR-300UV（车载） |
| TYT MD-UV380、MD-UV390、Retevis RT3S |
| TYT MD-9600、Retevis RT90（车载） |

DB25-D PRO 是 Crucible 新加入的电台：Crucible 自己的功能（数字 AGC、NXDN 电台默认设置）尚未在它上面测试。该电台没有模拟麦克风输入档位，因此“麦克风输入”显示为灰色。

各电台的详细状态见[英文 README](README.md#radios)。

## 安装与升级

在 Chrome 或 Edge 中打开 **[Crucible 刷机工具](https://tinker.wc8mi.net/crucible/)**：连接电台，选择版本，刷入。
它可以升级已运行 Crucible 或 RadioFoundry 的电台，保留语音编解码器和设置，也能备份和恢复。

版本发布在[发布页面](https://github.com/wc8mi/crucible/releases)。每个 zip 包含所有电台的固件、清单和许可声明。

## 获取帮助

- **Crucible 的问题：** 在这里[提交 issue](https://github.com/wc8mi/crucible/issues)，写明电台型号和
  **设置 > 高级 > 关于** 中显示的版本。
- **功能怎么用：** 共享功能见 RadioFoundry 的[用户手册](https://github.com/radiofoundry/radiofoundry/tree/main/docs/user)。
  Crucible 的不同之处见我们的[使用指南](docs/kf8sfork/README.md)。

## 贡献

**请向 [RadioFoundry](https://github.com/radiofoundry/radiofoundry) 贡献，而不是 Crucible。** 对任何电台有用的修复或功能都应提交到上游，
这样所有人都能用上，也会记在你的名下。Crucible 会在下次合并时带上它。请先阅读他们的[贡献指南](https://github.com/radiofoundry/radiofoundry/blob/main/docs/dev/contributing.md)。

Crucible 不接受外部拉取请求。欢迎报告 Crucible 本身的问题。如果 RadioFoundry 也有这个问题，请向 RadioFoundry 报告。

## 许可与致谢

MIT（[LICENSE](LICENSE)）。Crucible 基于 RadioFoundry，版权归 The RadioFoundry Authors 所有（[AUTHORS](AUTHORS)、
[CONTRIBUTORS](CONTRIBUTORS)）。Crucible 的改动版权归 David Behnke 所有。按照 [FORKING.md](FORKING.md)，许可声明随每个文件、
固件和版本保留。第三方许可见 [LICENSES/](LICENSES/)。
