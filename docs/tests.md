# ra9fael.qr 测试规格

## 1 单元测试（纯逻辑，`tests/test_qr.exe`，ctest 驱动）

编码器正确性（最高优先）：

- 用 bosskey 已验证向量做逐位比对：`https://www.windows.com/stopcode`（H 级）矩阵完全一致
- 独立交叉验证：生成的 PNG 用外部解码器（zxing-cpp 命令行或手机）解码回原文；流程与样本记录在 `tools/decode-check.md`
- 各 ECC 等级 × 边界长度（1 字节、每个版本临界长度 ±1）不崩溃、位长合法
- 数字/字母数字/字节模式自动选择正确
- 掩码评估与格式信息位（含转置）与已知向量一致

qrwifi：

- SSID/密码含 `\` `;` `,` `:` `"` 空格 的转义表逐项断言
- 密码为空 → `T:nopass` 且无 P 段；密码含 "WEP" 字样 → `T:WEP`
- 仅 SSID 无密码、SSID 含空格（第一个空格分隔）

已保存网络（`parseWifiProfile` 等纯逻辑）：

- 真实 v1 profile XML → SSID / `<authentication>` / 明文 key / `protected` / `useOneX` 逐项断言
- 可分享性四态：WPA2/WPA3 personal + 明文 key → `Ready`；无 sharedKey → `Open`（`T:nopass`）；`protected=true` → `NeedsKey`；`useOneX=true` → `Enterprise`，后两者不产出 payload
- 组策略 profile（`groupPolicy=true`，key 清空）→ `NeedsKey`，不产出 payload
- 所有用户 profile（`allUser=true`，key 清空）→ `NeedsKey`，不产出 payload
- 读取失败（`readFailed=true`）→ `NeedsKey` 而非 `Open`，不产出 payload
- 原因注记 `wifiKeyNote`：Ready → 「含密码」；readFailed / groupPolicy / allUser 各写明自己的原因（中英两语）；无已知原因保持「读不到密码，无法生成」
- 行 id 按 SSID 唯一：三个不同 SSID 得到三个不同 id（宿主对重复 id 只保留第一行）
- 认证令牌：WPA2PSK / WPA3SAE → `WPA`，WEP → `WEP`，open → `nopass`
- 只有 `<hex>` 的 SSID 解成 UTF-8；`&amp;` 等实体解码
- SSID 筛选：空匹配全部、大小写不敏感、子串

其余逻辑：

- 设置解析：`moduleSize`/`quietZone` 越界回默认；非法颜色串回默认
- 保存路径时间戳冲突 +1、不覆盖已有文件
- 空查询 + 剪贴板状态组合的提示行选择

目标：≥30 用例。

## 2 协议往返（对真实进程）

- `initialize → query（非空文本）→ action copyImage → shutdown` 全链路 JSON-RPC
- `qrwifi` 留空 → 列出本机已保存网络（本机实测 3 行：已连接的含密码、开放网络、未连接的含密码）；行里只有 SSID，密码不进 `data`
- 对已保存网络行发 `copyImage` → 动作时重读 profile 并生成 `WIFI:T:WPA;S:<ssid>;P:<key>;;`
- `qrwifi SSID 密码`（无匹配已保存网络）→ 回退手动形式
- query 空文本：宿主注入剪贴板文本后正常返回；无文本返回提示行
- `settingsChanged`（ECC L→M）后下一次 query 矩阵变化
- cancel：长文本查询中途取消不崩溃、不回复孤儿帧
- `timeoutMs` 1500 内必须响应

## 3 手动 E2E 清单（Windows 11，宿主 0.22.0+）

- [ ] `qr baidu.com` → Enter，手机扫码可打开
- [ ] 复制 URL 后留空查询 → 生成正确
- [ ] Ctrl+Enter 预览：置顶、Esc/点外关闭；**连续开/关 20 次**（二次打开回归——binarylab 崩溃教训）
- [ ] Shift+Enter 存图：Pictures 出现 PNG，重名 +1；手机扫存盘 PNG 成功
- [ ] 深色主题下预览窗底色为浅色（码内留白仍是 lightColor，不随主题反转）
- [ ] `qrwifi` 留空 → 列出已连接过的网络；Enter 复制，手机扫码连接成功
- [ ] `qrwifi` 输入片段 → 只留匹配的网络；输入不存在的 `SSID 密码` → 手动形式照旧
- [ ] 本机全部已保存网络都列出（按 SSID 唯一 id，不再被宿主按 id 去重吞行）
- [ ] 802.1X / 读不到密码的网络显示为不可选并写明原因；→ 菜单里复制图片/预览/保存 PNG 仍在（802.1X 除外），执行时如实报错
- [ ] 所有用户配置的网络：副标题写「保存为所有用户配置，明文密码需管理员读取」；以管理员运行宿主后重读可得明文
- [ ] `qrwifi` SSID 密码含 `;` 与空格 → 手机扫码连接成功
- [ ] 剪贴板是图片时留空查询 → 提示行
- [ ] 中英文 locale 文案正确

## 4 验收标准

- 单测全绿；协议往返通过；E2E 清单全勾
- 矩阵与独立解码交叉验证至少 3 组样本（URL/中文文本/Wi-Fi 格式）
