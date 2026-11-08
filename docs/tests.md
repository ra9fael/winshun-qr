# 二维码插件测试（v0.2.0）

- `qr_tests`：编码矩阵、版本和容量边界、ECC、文本分类、渲染颜色和留白、设置回退、保存文件名冲突。
- `tools/verify/hostsim.py`：JSON-RPC 初始化、文本查询、已删除命令返回空结果/空动作、剪贴板图片、连续打开预览、窗口前台及键盘焦点、Esc 关闭。
- `tools/verify/qr_check.py`：可用 OpenCV 独立解码单测导出的矩阵。

发版记录见 [validation-0.2.0.md](validation-0.2.0.md)。Wi-Fi 载荷、密码读取失败、开放/企业网络和图片分享测试归 ra9fael.wifi 管理。

人工验收：宿主菜单快捷键、深浅色主题、缩放、手机扫码。自动解码不代表已完成真实手机连接测试。
