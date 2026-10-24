# 原生 Qt/QML 网页方向轮盘（重做版）

本目录是与终端 Qt Quick/WebEngineView 架构一致的独立新实现，不修改原仓库文件。
上级 HTML 原型不是本实现的依赖，也不作为原生验收证据。

## 文件与接口

- `qml/DirectionWheel.qml`：184×184 逻辑像素，X 字分区，四向锁定，中心死区、连续幅度反馈、松手归中、键盘与可访问语义。
- `qml/BrowserWheel.qml`：绑定宿主的 `targetView`（真实 WebEngineView），`switchedOn` 默认 false。目标切换、加载、URL 变化、隐藏、窗口失焦均停止。
- `qml/ContextScroll.js`：仅网页 DOM 操作，最近指针/焦点上下文、嵌套祖先、textarea、同源 iframe、开放 shadow DOM、overscroll 边界与页面中心回退。
- `qml/Demo.qml`：原生开关 + 真实 WebEngineView 演示，不加载网络页面，不替换 Viper profile。

每帧按照 `zoomFactor` 校正像素速度。初始化和滚动 JS 共用单请求在途状态；超时后归中并禁用，成功安装新文档追踪器后恢复。超时不能撤销已经提交给 WebEngine 的脚本，迟到回调由 token 隔离，不得影响新请求。关闭开关不会取消已经交给 WebEngine 的一次脚本，但不会再提交下一帧。

## 构建

要求 CMake 3.21+、C++17、Qt 6.4+（Quick/QuickControls2/WebEngineQuick/QuickTest）及 Qt 所需的 OpenGL 开发头文件。

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build build --parallel 2
./build/wheel_demo
ctest --test-dir build --output-on-failure
```

可在已安装的 Qt 6 `qmltestrunner` 上直接运行 QML 单元与真实 WebEngine 测试：

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software /path/to/qt/bin/qmltestrunner -input tests
```

不禁用 Chromium 沙箱。单元测试含鼠标拖动、死区、隐藏/加载停止、单请求超时、幅度限幅与缩放；WebEngine 测试使用本地 HTML，不以模拟 DOM 代替真实滚动。

## 验收边界

没有修改 Main.qml、现有摇杆或 Viper 服务。产品接入时由宿主开关控制 `switchedOn`，当前标签实际 WebEngineView 绑定 `targetView`；宿主保持 Viper profile/过滤/书签/历史不变。独立演示使用 `WheelStrings.js` 中英文词表；宿主可绑定 `translate: function(text) { return I18n.t(text) }`，并将对应词条加入宿主词表。此接口不等于已完成宿主 I18n 接入。
主机测试不等于树莓派验收；ARM 构建、触屏取消/失焦、真实标签切换、性能测试与代码审查仍须在目标环境完成。VLC 视频验收不属于此次摇杆重做范围。
