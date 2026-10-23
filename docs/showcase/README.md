# README 展示资源

## 连接状态工作台

同一下方本地 HTTP 服务打开 `http://127.0.0.1:8000/docs/showcase/connection.html`。

`connection.html` 是纯前端源码语义演示，不连接设备，也不读写真实产品配置。`docs/assets/connection-atelier.svg` 使用 Anatomy `kit/iso-kit.mjs` 的 `slabOf`、`cylinder`、`solidSvg`、`fitProjection`、`topMatrix`、`faceTextSvg` 等方法生成 65 个实体部件。依据其指南采用四阶实体阴影、倒角、安装脚、板上器件、插接导轨和面内屏幕；插接运动采用时间步进的指数跟随，离屏/后台停止，减少动态效果时直接落位。造型是连接工作原理的概念装置，不是 STL 几何或真实 pinout。

状态边依据 `pi_terminal/src/core/SessionStateMachine.cpp`，ready、接收、信任、恢复及释放顺序依据 `ConnectionManager.cpp`，传输约束依据 `TransportPolicy.h`。画面、屏幕文字、指示灯和侧栏只读取同一个演示模型。未加入虚构的测量、下载百分比或吞吐量；自动接入在首次信任处暂停，需要用户明确操作。新图和 Word 文档链接在 README 技术图解中，原外壳 WebGL 浏览器保持独立入口。

Anatomy 来源：<https://github.com/wheresryan22/anatomy>，使用提交 `1482e8469b0a9e0d8cc18ea2740a01423d29ae80`。完整 MIT 许可证保留于生成 SVG 的 XML 注释（© 2026 Ryan）。静态 SVG 已包含所需样式和几何，没有远程依赖；运行页面不需要安装 Anatomy，也不需要保留其整份源码仓库。

## 模型浏览页

在仓库根目录执行：

```sh
python3 -m http.server 8000 --bind 127.0.0.1
```

打开 `http://127.0.0.1:8000/docs/showcase/`。服务只监听本机，结束时按 Ctrl+C。不要在包含私有配置的工作目录开启对外网监听的文件服务。

`index.html` 使用浏览器原生 WebGL，无第三方 CDN、包管理器或构建步骤。它直接解析 `hardware/enclosure/` 中的二进制 STL，按需绘制，不自动旋转；通过鼠标/触屏拖动、方向键、缩放按钮与 Home 键操作。无 WebGL 时显示静态图片。模型尺寸为 STL 坐标包围盒，依项目约定按毫米解释，不能当作装配或制造验证。

GitHub README 不运行 JavaScript 或 WebGL，也不支持 iframe 嵌入此页面。因此 README 使用静态 SVG/PNG 展示；仓库 HTML 链接是源文件入口，不是已部署的线上站点。本次不配置 GitHub Pages、不发布网站。

## 封面与模型图重建

```sh
# 需要 Python 3、NumPy、Pillow；在自己的虚拟环境准备依赖
python3 tools/generate_readme_art.py
```

输出：

- `docs/assets/terminal-hero.svg`：矢量排版与背景，内嵌真实 STL 的深度缓冲渲染图，脱离网络可显示。
- `docs/assets/enclosure-gallery.png`：四类模型的静态陈列图。

几何来源：

- 主机：`host-shell/stl/Shell_Lower.stl` 和 `Shell_Upper.stl`；封面仅将上壳上移 42 个坐标单位，以便观察。
- 圆台：`cone-case/stl/Cone_Case_Body.stl`。
- 底座：`pot-base/stl/Pot_Base_FINAL_内壁3mm_倒角优化.stl`。
- 支架：`stand/stl/Stand.stl`。

主机没有补画不存在的屏幕、面板或电路板；圆台没有补画盖件。色彩是展示材质，不代表打印材料或零件真实颜色。图中三角面数量从 STL 读取，不是估计值。静态渲染器采用正交投影与深度缓冲，仍不替代 CAD 工具的干涉、拓扑、配合或支撑检查。

本节的真实外壳图由项目 STL 自行渲染；连接工作台另行使用 Anatomy 生成 SVG，来源与许可见上节。
