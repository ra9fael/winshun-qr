# 二维码插件开发

`qr.exe` 是无 Qt 依赖的引导器，加载 `QrApp.dll`。GUI 主线程运行 Qt Quick 预览，工作线程处理 Win顺 JSON-RPC。`main.cpp` 映射唯一的 `qr` 命令；`qrcode.h` 实现编码器，`logic.h` 处理设置、文本分类、像素渲染和文件命名。

预览代码在 `qmlapp/QrPreview.cpp`。新预览先关闭旧窗口并清理 DeferredDelete，再创建、显示、激活并聚焦窗口，确保 Esc 可立即使用。剪贴板图片操作也调度到 GUI 线程。

Wi-Fi 查询、凭据读取和 Wi-Fi 载荷逻辑已迁入独立的 ra9fael.wifi；本库不链接 wlanapi。两个插件没有运行时依赖。

## 构建和测试

在 Visual Studio 开发环境中配置 Qt 路径：

```powershell
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH=D:/tools/Qt/6.12.0/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python tools/verify/hostsim.py
```

`hostsim.py` 默认使用 `C:\Program Files\WinShun` 的 Qt，可用 `QR_WS_DIR` 覆盖。它验证文本查询、未知命令拒绝、预览获取前台和键盘焦点以及 Esc 关闭。`noshow` 跳过窗口检查。

构建后执行 `python tools/package.py --tag v0.2.0`。发布前更新 plugin.json、资源版本和中英文 CHANGELOG，参考 [发布流程](release.md)。
