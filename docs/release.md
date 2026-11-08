# ra9fael.qr 发布与安装流程

## 构建

参照 bosskey 架构：`qr.exe` 薄引导器（静态链接 CRT）+ `QrApp.dll`（链接宿主共享 Qt 运行时，`WS_QT_DIR` 指向 D:\tools\Qt\6.12.0\msvc2022_64）。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物落在仓库根（`qr.exe`、`QrApp.dll`，与 plugin.json 同目录）。

## 打包（.wsplugin）

1. 确认 `plugin.json` 的 `version` 与发布 tag 一致（可用 `python tools/package.py --tag v<ver>` 强校验）
2. `python tools/package.py` 生成 `dist/ra9fael.qr-<ver>.wsplugin`（zip）：包含**所有 git 跟踪的文件**（源码、文档、许可证——投稿 unreviewed-native 要求包里带源码），外加构建产物 `qr.exe` 与 `QrApp.dll`（未入库）；`tools/verify/` 与点开头的文件（`.github` 等）不收。zip 是确定性的（文件名排序、固定时间戳、仅存储不压缩），同样输入必得同样 sha256
3. SHA-256 由打包脚本算好写在 `dist/<包名>.sha256`，也会追加到 `dist/release-notes.md`（取自 CHANGELOG 里对应的版本小节）；也可用 `Get-FileHash dist\ra9fael.qr-<ver>.wsplugin` 复核

## 发布

1. 先手动运行 `release.yml`，指定源码 `ref` 且 `publish=false`；通过后创建并单独推送 `v<ver>` 标签：`git tag -a v<ver> -m "Release v<ver>"`、`git push origin v<ver>`
2. GitHub Actions 自动创建 Release `v<ver>` 并上传 `.wsplugin`；release notes 写变更点、SHA-256 与构建命令（复述 README 的 provenance 段）
3. 声明为社区原生包（`unreviewed-native`）：公开源码、构建命令与 SHA-256 佐证来源，但安装仍需用户逐次确认

## 安装（用户）

- 方式 A（宿主界面）：Win顺 → 插件管理 → 本地安装 → 选择 `.wsplugin` → 确认（unreviewed-native 提示）→ 停用再启用插件（或重启宿主）加载新 manifest
- 方式 B（手动）：解包到 `%LOCALAPPDATA%\WinShun\plugins\ra9fael.qr\`（`plugin.json` 必须在目录根部）→ 重启 Win顺
- 首次启用弹权限确认（剪贴板读写 / 窗口 / Pictures 写入）

## 升级

- 覆盖安装新版 `.wsplugin`，或替换目录内文件后停用/启用插件
- 无数据迁移（本插件不持久化）

## 卸载

- 宿主中停用 + 删除，或直接删除插件目录后重启宿主
- 残留：无（不写数据目录）；预览窗口随插件进程退出

## 回滚

- GitHub Release 保留历史版本；下载旧 `.wsplugin` 覆盖安装即可


当前发布入口仅为新版本 tag 与手动运行。新 tag 会自动构建、测试、打包并发布；已有 tag 更新/删除、分支 push 和 PR 不触发。手动发布须指定已有版本 tag 并设置 `publish=true`，不得覆盖已有 Release 附件。完整参数见 [WORKFLOWS.md](../WORKFLOWS.md)。
