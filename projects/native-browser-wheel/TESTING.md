# 本次验证记录

环境：Linux x86_64，Qt 6.5.3，Qt Quick 软件离屏渲染。

执行：

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  /home/drdusk/.local/share/orange-pi-terminal-sdk/qt/6.5.3/gcc_64/bin/qmltestrunner \
  -input tests
```

在本项目目录运行。最新完整回归：18 passed，0 failed，0 skipped（包含初始化与清理）。测试日志可用 qmltestrunner 的 `-o artifacts/test.log,txt` 参数保存（先创建 artifacts 目录）。

- QML 鼠标按下/拖动/释放控制真实 WebEngine 页面，验证滚动发生、松手停止和归中；不强行设置控制器内部状态。
- 真实 DOM 嵌套区水平/垂直滚动、边界传递、overscroll contain、textarea 与开放 Shadow DOM。
- QML 死区、连续幅度、边缘限幅、默认关闭、键盘松手、目标切换、加载/隐藏停止。
- 异步模拟 WebEngine：初始化和滚动共用在途状态，超时停止、新文档加载后恢复；迟到旧回调不能清除新请求。

局限：直接使用发行版 qmltestrunner 会提示 WebEngine 从插件初始化；测试未禁用 Chromium 沙箱。提供的 C++ runner 在 QGuiApplication 之前初始化 WebEngine，待完成构建后使用该 runner 回归。

CMake 配置已实际尝试，但因主机缺少 OpenGL 开发头文件（OPENGL_INCLUDE_DIR，WrapOpenGL）失败，因此**没有宣称 C++ demo/runner 构建通过**。Qt/QML 组件和真实 WebEngine 交互通过的是上面的直接运行测试。

尚未验证：树莓派 ARM 构建、真实触屏、产品 Viper profile 接入、性能预算及宿主 I18n.t 接入。自动测试通过不代表全部设备端技术规范验收完成。
