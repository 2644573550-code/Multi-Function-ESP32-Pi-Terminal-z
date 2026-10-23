# 外壳 CAD 文件与资料说明

本文按 `hardware/enclosure/` 中现有资料说明模型类别、文件格式、配套报告和脚本用途。模型的具体尺寸、打印参数及适用版本，以相应模型目录的 `README.md` 和专门测试报告为准。

## 文件格式

| 格式 | 文件用途 |
|---|---|
| `.blend` | Blender 场景文件，可用于查看或继续编辑模型、检查模型组织，并从 Blender 导出模型。 |
| `.FCStd` | FreeCAD 原生文档。用于查看 FreeCAD 中的建模结构；是否可完整参数化编辑取决于具体文件内容。 |
| `.step` | CAD 交换模型，适合导入 FreeCAD 等机械 CAD 软件继续查看或处理。STEP 与原生工程文件不是同一种格式。 |
| `.stl` | 网格模型，常用于切片和 3D 打印。STL 通常不保留原生 CAD 特征。 |
| `.3mf` | 面向 3D 打印的模型/项目文件。目录中的文件按单件或拼盘区分；打印前仍应在切片软件中检查摆放和设置。 |
| `.txt` | 中文装配说明、参数速查、设计过程记录或测试报告。 |
| `.py` | FreeCAD Python 建模、重建、打包或打印文件生成脚本。运行前需检查脚本中的路径和 FreeCAD 环境依赖。 |

## 主机外壳：`hardware/enclosure/host-shell/`

桌面主机的上下壳及面板、底盖、走线槽盖等配套零件。目录提供整体模型、上下分体模型、各零件导出件和打印拼盘。

### 可编辑与交换模型

- `blender/Host_Shell.blend`：主机外壳 Blender 场景。
- `blender/Host_Shell_split.blend`：分体版本 Blender 场景。
- `freecad/Host_Shell.FCStd`：FreeCAD 原生文件。
- `freecad/Host_Shell.step`、`freecad/Host_Shell.stl`：与 FreeCAD 文件并列的整体 STEP、STL 导出文件。

### 零件文件

- `stl/Shell_Lower.stl`、`Shell_Upper.stl`：下半部与上半部外壳网格。
- `stl/Shell_Lower_A.stl`、`Shell_Lower_B.stl`：下壳分段打印用网格；分段位置与装配关系见“下壳分两段说明”。
- `stl/Front_Cover.stl`、`Back_Cover.stl`：正面面板和背板网格。
- `stl/Bottom_Cover.stl`：底面盖板网格。
- `stl/Channel_Cover.stl`：走线槽盖网格。
- `step/Front_Cover.step`、`Back_Cover.step`、`Bottom_Cover.step`、`Channel_Cover.step`：上述面板/盖件的 STEP 交换文件。

### 打印布局文件

`3mf/` 中单件文件按编号说明对象：

1. `打印_1_下半部外壳.3mf`
2. `打印_2_上半部外壳.3mf`
3. `打印_3_正面面板.3mf`
4. `打印_4_背板.3mf`
5. `打印_5_底面盖板.3mf`
6. `打印_6_走线槽盖.3mf`
7. `打印_7_下壳A段(z0~75).3mf`
8. `打印_8_下壳B段(z75~150).3mf`

另有四个拼盘文件：`打印_拼盘A_正面面板+走线槽盖.3mf`、`打印_拼盘B_背板+底面盖板.3mf`、`打印_拼盘C_下半部外壳+上半部外壳.3mf`、`打印_拼盘D_下壳A段+下壳B段.3mf`。拼盘文件用于查看其包含的组合和排布；具体打印顺序、方向、支撑与裙边参数见 `docs/上下壳拼盘说明.txt`、`docs/打印参数_分件速查.txt`。

## 圆台外壳：`hardware/enclosure/cone-case/`

便携终端圆台形外壳，文件按主体（Body）与盖件（Lid）提供：

- `blender/cone-case.blend`：Blender 模型场景。
- `freecad/1_圆台外壳(窗29_总高68_适配44PCB).FCStd`：文件名注明屏幕窗、总高和 PCB 适配信息的 FreeCAD 版本。
- `freecad/2_圆台壳体(花盆外罩).FCStd`：花盆外罩相关 FreeCAD 版本。
- `step/Cone_Case_Body.step`、`step/Cone_Case_Lid.step`：主体和盖件 STEP 文件。
- `stl/Cone_Case_Body.stl`、`stl/Cone_Case_Lid.stl`：主体和盖件网格文件。
- `3mf/Cone_Case_Body.3mf`、`3mf/Cone_Case_Lid.3mf`：主体和盖件分开的打印文件；`3mf/Cone_Case.3mf` 为组合文件。

屏幕窗的设计变更记录见 `docs/屏幕窗改动说明.txt`。圆台与底座之间的螺旋配合参数和复检结果见 `docs/螺旋配合复检报告.txt`、`docs/F1精确版测试报告.txt` 及推荐档位测试报告。

## 花盆底座：`hardware/enclosure/pot-base/`

圆台外壳配套的底座/托盘模型。此目录包含不同加工阶段和用途的版本，应依据文件名及对应报告选择，不宜把不同版本的 STEP、STL 与 FreeCAD 原文件视作同一模型的完全等价导出。

- `blender/pot-base.blend`：Blender 模型场景。
- `freecad/3_花盆托盘底座.FCStd`：FreeCAD 原生底座模型。
- `step/Pot_Base.step`：底座通用 STEP 文件。
- `step/Pot_Base_F1_精确.step`：F1 精确版本 STEP。
- `step/Pot_Base_F3_原版Brep.step`：F3 原版 B-rep STEP。
- `stl/Pot_Base.stl`：基础版 STL。
- `stl/Pot_Base_FINAL_内壁3mm_倒角优化.stl`：文件名注明内壁厚度和倒角优化的最终 STL 版本。
- `stl/临时底座_上防滑_下定位.stl`：临时底座网格，文件名注明上表面防滑、下侧定位特征。
- `3mf/Pot_Base.3mf`：底座打印项目文件。

螺纹/间隙与不同版本的实测记录见 `docs/螺旋配合复检报告.txt`、`docs/F1精确版测试报告.txt`、`docs/推荐档1005全套测试.txt`、`docs/打印误差冗余核算.txt`、`docs/临时底座v9装配测试.txt`。底板强度资料见 `docs/防滑底板厚度与强度.txt`；拆件方案评估见 `docs/拆两半可行性评估.txt`。

## 一体化支架：`hardware/enclosure/stand/`

一体化支架的多格式模型：

- `blender/stand.blend`：Blender 场景文件。
- `freecad/One_Piece_Stand.FCStd`：FreeCAD 原生文件。
- `step/Stand.step`：STEP 交换文件。
- `stl/Stand.stl`：打印网格文件。
- `3mf/One_Piece_Stand.3mf`：打印项目文件。

打印方向及参数见 `docs/打印参数_一体化支架.txt`。

## 配套说明与测试报告：`hardware/enclosure/docs/`

| 文件 | 资料内容 |
|---|---|
| `整机安装说明.txt` | 整机及外壳装配步骤、零件安装顺序和相关操作说明。 |
| `整机装拆测试(含屏幕).txt` | 安装屏幕后进行的整机装配、拆卸测试记录。 |
| `分体版说明.txt` | 上下分体结构和分体版本相关说明。 |
| `下壳分两段说明.txt` | 下壳 A/B 分段设计、分段位置和连接说明。 |
| `下壳B段避开网口说明.txt` | 下壳 B 段与网络接口位置之间的避让说明。 |
| `上下壳拼盘说明.txt` | 多个打印拼盘的组成和排布说明。 |
| `打印参数_分件速查.txt` | 外壳分件的打印方向、支撑及切片参数速查。 |
| `打印参数_一体化支架.txt` | 一体化支架的打印参数。 |
| `屏幕窗改动说明.txt` | 圆台外壳屏幕窗口的改动记录。 |
| `槽盖最终验证报告.txt` | 走线槽盖的最终结构和验证记录。 |
| `螺旋配合复检报告.txt` | 圆台外壳与底座螺旋配合的复检资料。 |
| `F1精确版测试报告.txt` | F1 精确版本的测试记录。 |
| `推荐档1005全套测试.txt` | 推荐配合档位的全套测试记录。 |
| `打印误差冗余核算.txt` | 打印误差、间隙和冗余量的核算资料。 |
| `临时底座v9装配测试.txt` | 临时底座 v9 的装配测试记录。 |
| `拆两半可行性评估.txt` | 底座拆分为两部分的可行性评估。 |
| `防滑底板厚度与强度.txt` | 防滑底板厚度和结构强度相关分析。 |

这些 TXT 文件是设计和测试资料，记录范围、版本和测试条件因文件而异。使用某个模型时，应优先查阅与该模型同名或同版本的说明，不要把一份版本报告套用到目录中的所有模型。

## 建模与打印脚本：`hardware/enclosure/scripts/`

- `build_v32.py`：FreeCAD Python 主机外壳 v32 构建脚本，包含参数和几何创建逻辑。
- `rebuild_v32.py`：主机外壳 v32 重建辅助脚本。
- `pack_v32.py`：v32 模型打包辅助脚本。
- `make_print_files.py`：生成打印用文件的辅助脚本。

脚本依赖 FreeCAD Python API。执行前先检查脚本内输入/输出路径、版本和参数；特别是旧脚本可能保留作者环境中的绝对路径，需根据当前机器调整。脚本输出不应未经检查就覆盖目录中的现有模型或打印文件。

## 顶层说明文件

- `hardware/enclosure/README.md`：外壳项目的主要索引，进一步说明零件规格、尺寸、结构特征、装配要点、打印建议、已知问题及各份 TXT 文档的内容。
- `hardware/enclosure/SSH上传说明.txt`：通过 SSH 上传相关文件的操作说明。
