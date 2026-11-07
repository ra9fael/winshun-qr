# ra9fael.qr 功能规格（v0.1.0）

状态：设计完成，代码未实现。协议与清单字段以 WinShun 0.22.0 插件文档为准（resident JSON-RPC over stdio）。

## 1 目标

- 在搜索框里把文本变成二维码图，键盘直达：输入 → Enter 复制图片
- Wi-Fi 场景：`qrwifi SSID 密码` 生成扫码即连的二维码（标准 `WIFI:` 格式）
- 图像质量可控：纠错级别、模块尺寸、留白、颜色

## 2 非目标（v0.1）

- 不做扫码/读码，只生成
- 不做美化（logo、圆点、渐变）
- 不做批量生成
- 不监听剪贴板自动弹码（打扰且涉隐私）；只在查询留空时主动读一次

## 3 命令

### qr（关键字 `qr` / `二维码`）

- 查询非空：作为待编码文本，UTF-8，≤2953 字节；超出返回 `valid:false` 提示行
- 查询为空：读剪贴板文本（`clipboard:read`）；剪贴板无文本 → 提示行
- 结果一行：subtitle 标注类型判定（URL / 文本 / Wi-Fi 格式）+ QR 版本 + ECC + 字节数
- 动作：
  - `Enter copyImage`：二维码 QImage 写入剪贴板（`QClipboard::setImage`）；成功后结果行 subtitle 更新「已复制」，不弹通知
  - `Ctrl+Enter preview`：打开预览窗口（置顶、点击窗外关闭、Esc 关闭），展示二维码 + 原文
  - `Shift+Enter savePng`：写 `%USERPROFILE%\Pictures\qr-<yyyymmdd-hhmmss>.png`；同秒冲突时文件名 +1，不覆盖已有文件；结果行带回实际路径

### qrwifi（关键字 `qrwifi`）

- 参数 `SSID 密码`，以第一个空格分隔；密码可空 = 开放网络
- 生成 `WIFI:T:<WPA|WEP|nopass>;S:<ssid>;P:<password>;;`：
  - 密码非空按 WPA，含 WEP 字样按 WEP，空按 `T:nopass` 并省略 P 段
  - SSID/密码中的 `\` `;` `,` `:` `"` 按规范以 `\` 转义
- 动作与 qr 相同

## 4 行为规格

- 编码器：整体复用 ra9fael.bosskey 的 `qrcode.h`（格式信息、掩码、对齐图形已验证），不重写
- 渲染：矩阵 + quietZone → `QImage::Format_RGB32`（不直接用 Mono，保证剪贴板兼容）；颜色设置只影响渲染，不影响矩阵
- 设置变更（`settingsChanged`）即时生效
- 预览窗口生命周期：**创建新窗口前先同步关闭并销毁旧实例**（binarylab 二次打开 HWND 冲突崩溃的既有多米诺）；窗口关闭不退出插件进程
- 结构：qr.exe 薄引导器 + QrApp.dll（QML 预览），Qt 6.12 由宿主共享运行时提供（`WS_QT_DIR`），参照 bosskey 架构

## 5 设置

`ecc` / `moduleSize` / `quietZone` / `darkColor` / `lightColor`（见 [plugin.json](../plugin.json)），越界或非法颜色回默认值。

## 6 权限

- `clipboard:read`（空查询读剪贴板）、`clipboard:write`（复制图片）
- `ui`（预览窗口）
- `files:write` 限定 `%USERPROFILE%\Pictures`
- 不做 `network`、`processes`、`elevated`

## 7 数据位置

不持久化；`WS_DATA_DIR` 未用；无网络、无遥测。

## 8 失败处理

- 文本超容量 → `valid:false` 提示行，写明当前 ECC 下最长长度
- 剪贴板非文本 → 提示行
- Pictures 不可写（重定向/权限）→ 报错行带具体原因
- 编码失败（理论不可达）→ 提示行，绝不写空图

## 9 里程碑

- v0.1：qr + qrwifi + 三动作 + 上述设置
- v0.2 候选：文件选区触发（对选中文件生成内容哈希码）、logo 叠加
