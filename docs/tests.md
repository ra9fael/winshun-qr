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

其余逻辑：

- 设置解析：`moduleSize`/`quietZone` 越界回默认；非法颜色串回默认
- 保存路径时间戳冲突 +1、不覆盖已有文件
- 空查询 + 剪贴板状态组合的提示行选择

目标：≥30 用例。

## 2 协议往返（对真实进程）

- `initialize → query（非空文本）→ action copyImage → shutdown` 全链路 JSON-RPC
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
- [ ] `qrwifi` SSID 密码含 `;` 与空格 → 手机扫码连接成功
- [ ] 剪贴板是图片时留空查询 → 提示行
- [ ] 中英文 locale 文案正确

## 4 验收标准

- 单测全绿；协议往返通过；E2E 清单全勾
- 矩阵与独立解码交叉验证至少 3 组样本（URL/中文文本/Wi-Fi 格式）
