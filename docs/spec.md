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

- 留空：列出本机**连接过**的 Wi-Fi（Windows 已保存的 profile），一行一个 SSID，subtitle 标注认证方式、是否当前连接、是否含密码；行内动作与 qr 相同
- 输入文字：按 SSID 子串（不区分大小写）筛选上面的列表；一个都没匹配上时回退为手动形式 `SSID 密码`
- 手动形式：参数 `SSID 密码`，以第一个空格分隔；密码可空 = 开放网络
- 生成 `WIFI:T:<WPA|WEP|nopass>;S:<ssid>;P:<password>;;`：
  - 已保存 profile 的认证取自 profile XML 的 `<authentication>`：WPA / WPA2 / WPA3（含 `SAE`）→ `WPA`（Wi-Fi 二维码约定没有 WPA3 令牌），`WEP` → `WEP`，`open` → `nopass`
  - 手动形式沿用旧规则：密码非空按 WPA，含 WEP 字样按 WEP，空按 `T:nopass` 并省略 P 段
  - SSID/密码中的 `\` `;` `,` `:` `"` 按规范以 `\` 转义
- 不能分享的网络也列出，但标记为不可选（`valid:false`）：802.1X（没有共享密码）、密码读不到（profile 里仍是受保护的 blob）
- 动作与 qr 相同

### 3.1 读取已保存网络的密码（实测）

`WlanGetProfile` 带 `WLAN_PROFILE_GET_PLAINTEXT_KEY` 对当前用户自己的 profile **不需要管理员权限**即返回明文（2026-11-08 实测：非提权进程 `TokenIsElevated=0`，3 个 profile 全部 `protected=false`；与 ra9fael.wifi 的结论一致）。因此 `qrwifi` 直接列网络，不要求先输 SSID 和密码。

密码不进 item 的 `data`：行里只带 SSID，动作执行时再按 SSID 重读 profile。这样密码不落在宿主的 item 里，也不会因为列表刷新而变陈旧。拿不到明文时如实报错，不给假成功。

**注意**：本插件 `runtime.qt` 为 `host`（共享宿主 Qt），而宿主文档写明首版不支持 `elevated` 与共享 Qt 同时使用，所以清单**不声明** `elevated`；域环境、组策略下发的 profile 在普通权限下能否读到明文未验证，读不到时走「标记不可选」路径。

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
- 不做 `network`、`processes`、`elevated`：`qrwifi` 用 wlanapi 读本机已保存的 profile，是本地配置读取，不是联网；不起子进程，也不提权（见 §3.1）

## 7 数据位置

不持久化；`WS_DATA_DIR` 未用；无网络、无遥测。

## 8 失败处理

- 文本超容量 → `valid:false` 提示行，写明当前 ECC 下最长长度
- 剪贴板非文本 → 提示行
- Pictures 不可写（重定向/权限）→ 报错行带具体原因
- 编码失败（理论不可达）→ 提示行，绝不写空图
- `qrwifi` 读不到网络：无无线网卡 → 提示行（插网卡/开电台）；WlanSvc 未运行 → 提示行（启动 WlanSvc）；其他 wlanapi 错误 → 提示行带错误码
- `qrwifi` 没有已保存网络且未输入 → 提示行「没有已保存的 Wi-Fi」，副标题提示先连接一次或直接输入 SSID 密码
- 已保存网络的密码在动作时读不到（profile 被删/权限变化）→ 通知「这个 Wi-Fi 的密码现在读不到了」，不生成空图

## 9 里程碑

- v0.1：qr + qrwifi + 三动作 + 上述设置
- v0.2 候选：文件选区触发（对选中文件生成内容哈希码）、logo 叠加
