# 阿米娅动画素材说明

- 角色：《明日方舟》阿米娅，基建默认造型，`models/002_amiya`。
- 原始模型来源：https://github.com/isHarryh/Ark-Models 。固定提交和输入/输出 SHA256 见 `SOURCES.json`。
- 原始素材版权归上海鹰角网络科技有限公司/原版权方所有；来源仓库明确说明不得用于商业用途、不得损害版权方利益。随附 `ARK-MODELS-NOTICE.md` 保留上游完整说明。
- 这里只做本机非商业原型测试。素材不因本项目源码开源而成为自由授权资源；公开分发或商业用途应另行确认授权。
- 资源是离线渲染的透明 PNG 图集，不是 Ark-Pets Java 源码；没有复制其行为算法。待机/行走/互动状态机为本副本重新实现。
- 离线导出工具使用单独提供的 Spine Runtime 3.8.99.1 和 libGDX 1.12.1；运行时有独立授权条款，见 `SPINE-RUNTIMES-LICENSE.txt` 及 https://esotericsoftware.com/spine-editor-license 。不宣称 GPL 可覆盖 Spine 授权；重新运行导出/分发前需确认适用许可。
- 终端运行只需要 Qt Quick，**不包含/加载 Spine、Java 或 libGDX 运行时**。外部工具 JAR、原始骨骼和逐帧中间文件不放入终端资源。
