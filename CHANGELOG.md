# Changelog

[English](CHANGELOG.en.md)

All notable changes to ra9fael.qr. Format follows [Keep a Changelog](https://keepachangelog.com/).

## [0.1.1] - 2026-11-09

### Fixed

- 修复可执行文件图标，补齐打包中的 ICO 和资源脚本。 在 Qt 界面库中嵌入多尺寸 PNG 并设置应用图标；打包缺少界面 DLL 时拒绝生成。

### Notes

- 本次仅生成本地标签与安装包。实际验证范围见 docs/icon-validation.md；未进行 GitHub 发布。更新后需重新启动插件进程。


## [0.1.0] - 2026-11-08

- `qr`：文字/链接/剪贴板 → 二维码；Enter 复制图片、Ctrl+Enter 预览、Shift+Enter 存 PNG
- `qrwifi`：留空列出本机连接过的 Wi-Fi，生成扫码即连的二维码；也可 `qrwifi SSID 密码` 手动指定
- 预览窗先销毁旧实例再创建（回归过 HWND 冲突崩溃）
- 已保存网络列表：按 SSID 唯一 id（宿主对重复 id 只保留第一行）；读不到密码的网络也列出并写明原因（个人配置 / 组策略 / 读取失败），动作仍可重读
