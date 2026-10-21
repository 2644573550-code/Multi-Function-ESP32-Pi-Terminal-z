# 当前设备上的角色转换工具

`character_worker.py` 是独立后台任务：读取 Ark-Models 基建角色目录、仅下载所选角色、在当前设备编译运行 `ExportSprites.java`，最后以 `pack_sprites.py` 生成 PNG 图集。发布完整输出目录前不改变当前人物。工作进程通过一行一个 JSON 向 `PetLibrary` 报告进度/失败/输出目录，不包含服务控制、SSH 或提权逻辑。

依赖：Linux、系统 Python3/Pillow、JDK17、OpenGL 图形会话（或 Xvfb）。Java 转换依赖首次从固定 Maven 地址下载，版本和 SHA256 在 `runtime-lock.json`；x86_64/ARM64 分目录。缺少系统工具仅报告，不擅自安装。Spine 运行时有独立许可，角色有原版权限制，使用前见 `assets/arkpets/NOTICE.md` 和随附许可。

```sh
python3 character_worker.py catalog --root /absolute/path/to/arkpets
# 仅在已确认适用许可后：
python3 character_worker.py install --root /absolute/path/to/arkpets \
    --model '002_amiya_winter#1' --accept-licenses
```

导出器识别 Spine 3.8 binary/JSON 数据，在待机、移动、互动、坐下和躺下的采样姿态上测量共同边界，按所选质量输出帧；列数受帧宽和图集边长限制。打包器将 PMA 转回 straight alpha，并记录逐帧底边。缺少 Move/Interact/Sit/Sleep 时导出 Idle 回退，并显式标记 `supported=false`，UI 不启用不存在的姿势。

`conversionVersion=4`：`--quality economy|standard|high` 固定输出 128/256/384px 帧，各自 64/128/256 MiB 解码预算，图集行列同时受 2048×8192 边界约束。每动作至多 240 帧；完整最长 120 秒循环使用小数 FPS 保留时长，不因时长再降低纹理分辨率。

打包器输出 `frameFeet`（alpha >32 的每帧底边）、`referenceHeight`（待机可见高度中位数）与 `targetHeight=176`。Qt 对旧图集也重新测量这些字段，显示比例基于可见人物，不基于画布；所有动作共享比例但逐帧接地。支持目录的 `Operator/models` 与 `DynIllust/models_illust`；动态立绘可用非标准 Idle 名称回退，缺失 Move/Sit/Sleep 明确不支持。这里不是 Cubism `.model3.json` 加载器。

缓存键包含提交、角色、转换版本与档位，旧角色仍可显示并在管理页更新，不覆盖旧缓存。内置阿米娅为标准 256px 五动作；具体角色、档位及平台需要独立验证，不保留其他版本的转换验收结论。

只将 `.atlas/.skel/.png` 当作数据，不执行下载的模型代码；验证路径、纹理尺寸、下载体积、工具哈希与最终帧数。SIGTERM 会终止当前转换子进程组并退出临时工作目录。每个资料目录有互斥锁，避免并发工作进程覆盖缓存。
