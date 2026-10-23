#!/usr/bin/env python3
"""Regenerate the current, source-checked system figures in docs/figures/.

The companion technical-spec generator contains an earlier product proposal;
this file is the maintained renderer for the diagrams in the current source.
It intentionally draws role-level hardware links, not undocumented pinouts.
Requires matplotlib and Noto Sans CJK SC (or a system sans-serif fallback).
"""
from pathlib import Path
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Circle
from matplotlib.font_manager import FontProperties

ROOT = Path(__file__).resolve().parent
FIG = ROOT / "figures"
FIG.mkdir(exist_ok=True)

BG = "#F4F4ED"
PAPER = "#FFFFFF"
INK = "#183329"
MUTED = "#617166"
LINE = "#D5DCD2"
GREEN = "#25634B"
GREEN_BG = "#E7F1E9"
BLUE = "#3F6B91"
BLUE_BG = "#EAF0F7"
AMBER = "#9B6C20"
AMBER_BG = "#F6EFDF"
VIOLET = "#705A91"
VIOLET_BG = "#F0ECF5"
RED = "#A95442"
RED_BG = "#F7ECE7"
SLATE = "#586977"
SLATE_BG = "#EDF0F2"

FONT_PATH = Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc")
FONT = FontProperties(fname=str(FONT_PATH)) if FONT_PATH.exists() else FontProperties(family="sans-serif")
FONT_BOLD_PATH = Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc")
FONT_BOLD = FontProperties(fname=str(FONT_BOLD_PATH)) if FONT_BOLD_PATH.exists() else FONT


def rounded(ax, x, y, w, h, fill=PAPER, edge=LINE, radius=0.14, lw=1.15, z=1):
    patch = FancyBboxPatch((x, y), w, h,
        boxstyle=f"round,pad=0.02,rounding_size={radius}",
        linewidth=lw, edgecolor=edge, facecolor=fill, zorder=z)
    ax.add_patch(patch)
    return patch


def label(ax, x, y, text, size=11, color=INK, bold=False, ha="left", va="center", z=5, **kwargs):
    ax.text(x, y, text, fontsize=size, color=color, ha=ha, va=va,
            fontproperties=FONT_BOLD if bold else FONT, zorder=z, **kwargs)


def base(number, title, subtitle, note):
    fig, ax = plt.subplots(figsize=(14.55, 8.45), dpi=120)
    fig.patch.set_facecolor(BG)
    ax.set_facecolor(BG)
    ax.set_xlim(0, 16)
    ax.set_ylim(0, 9)
    ax.axis("off")
    rounded(ax, .22, .18, 15.56, 8.62, fill=BG, edge=BG, radius=.18, lw=0)
    label(ax, .62, 8.43, f"SYSTEM NOTES     /     {number:02d}", 8.5, GREEN, True)
    label(ax, .62, 7.97, title, 23, INK, True)
    label(ax, .64, 7.55, subtitle, 10, MUTED)
    ax.plot([.62, 15.38], [7.24, 7.24], color=LINE, lw=1, zorder=2)
    ax.plot([.62, 15.38], [.72, .72], color=LINE, lw=1, zorder=2)
    label(ax, .64, .45, note, 8.1, MUTED)
    label(ax, 15.35, .45, "PI TERMINAL   /   SOURCE-CHECKED", 7.6, MUTED, True, ha="right")
    return fig, ax


def card(ax, x, y, w, h, title, body="", accent=GREEN, fill=PAPER, tag=None, title_size=11.5, body_size=8.8):
    rounded(ax, x, y, w, h, fill=fill, edge=LINE, radius=.13, lw=1.0, z=2)
    ax.add_patch(FancyBboxPatch((x, y+.07), .055, h-.14,
        boxstyle="round,pad=0.01,rounding_size=.025", facecolor=accent,
        edgecolor=accent, lw=0, zorder=3))
    top = y+h-.24
    if tag:
        label(ax, x+.19, top, tag.upper(), 7, accent, True)
        top -= .30
    label(ax, x+.19, top, title, title_size, INK, True, va="top")
    if body:
        label(ax, x+.19, top-.34, body, body_size, MUTED, va="top", linespacing=1.45)


def pill(ax, x, y, w, text, color=GREEN, fill=GREEN_BG, size=7.5):
    rounded(ax, x, y, w, .30, fill=fill, edge=fill, radius=.13, lw=0, z=3)
    label(ax, x+w/2, y+.15, text, size, color, True, ha="center")


def arrow(ax, start, end, color=GREEN, text=None, text_xy=None, both=False, rad=0, lw=1.8, style="-"):
    arrowstyle = "<|-|>" if both else "-|>"
    patch = FancyArrowPatch(start, end, arrowstyle=arrowstyle, mutation_scale=12,
        linewidth=lw, color=color, linestyle=style,
        connectionstyle=f"arc3,rad={rad}", shrinkA=2, shrinkB=2, zorder=4)
    ax.add_patch(patch)
    if text:
        tx, ty = text_xy or ((start[0]+end[0])/2, (start[1]+end[1])/2+.16)
        rounded(ax, tx-.75, ty-.14, 1.5, .28, fill=BG, edge=BG, radius=.08, lw=0, z=5)
        label(ax, tx, ty, text, 7.4, color, True, ha="center", z=6)


def panel(ax, x, y, w, h, title, subtitle=None, accent=SLATE):
    rounded(ax, x, y, w, h, fill="#FBFCF9", edge=LINE, radius=.16, lw=1.0, z=1)
    label(ax, x+.22, y+h-.29, title, 12, accent, True)
    if subtitle:
        label(ax, x+.22, y+h-.57, subtitle, 8, MUTED)


def save(fig, filename):
    fig.savefig(FIG / filename, dpi=120, facecolor=BG, edgecolor=BG)
    plt.close(fig)


def draw_01():
    fig, ax = base(1, "模块系统架构", "应用运行时、控制桥、无线数据链路与 Pi 侧插件：按职责分层，而不是把业务塞进主程序。",
                   "逻辑架构示意；无线会话固定选 BLE 或热点 LAN。非引脚图，固件需按各自版本核对。")
    panel(ax, .58, 1.14, 5.20, 5.72, "树莓派终端", "Qt 6 / Linux · 插件只在 Pi 进程执行", BLUE)
    card(ax, .90, 5.02, 4.55, .98, "Qt Quick / QML", "主页、模块页、应用交互", BLUE, BLUE_BG, "界面", title_size=10)
    card(ax, .90, 3.79, 4.55, .96, "ConnectionManager", "状态机 · PogoControlTransport · 固定 BLE/LAN 策略", GREEN, GREEN_BG, "runtime + control", title_size=9.2, body_size=7.6)
    card(ax, .90, 2.50, 2.15, .96, "terminal_*", "系统 / 音乐 / UI", SLATE, SLATE_BG, "内置库", title_size=9.1, body_size=7.4)
    card(ax, 3.30, 2.50, 2.15, .96, "PackageReceiver", "临时接收 / 校验", AMBER, AMBER_BG, "package", title_size=8.7, body_size=7.4)
    card(ax, .90, 1.30, 4.55, .96, "ModuleManager · QLibrary", "Pi 侧 C ABI 插件 · QML 页面 · 同步生命周期", VIOLET, VIOLET_BG, "外接模块", title_size=9.7, body_size=7.6)

    panel(ax, 6.24, 5.13, 3.48, 1.73, "ESP32 Host / 控制桥", "PBR1 over USB serial", SLATE)
    card(ax, 6.56, 5.35, 2.83, .76, "控制事件与命令", "attach / detach · ping / status", SLATE, SLATE_BG, "control", title_size=9.0, body_size=7.3)

    panel(ax, 11.08, 2.72, 4.34, 4.14, "ESP32-S3 功能模块", "业务固件、设备与模块包来源", GREEN)
    card(ax, 11.40, 5.22, 3.70, .96, "模块控制与业务服务", "认证 · 数据事件 · 会话确认", GREEN, GREEN_BG, "module", title_size=9.6, body_size=7.5)
    card(ax, 11.40, 4.00, 3.70, .82, "业务数据服务", "认证事件 · 传感器 / 功能数据", BLUE, BLUE_BG, "device", title_size=9.4, body_size=7.5)
    card(ax, 11.40, 3.02, 3.70, .58, "签名插件包（若已提供）", accent=AMBER, fill=AMBER_BG, title_size=8.3)

    # Independent paths stay in the whitespace between component panels.
    arrow(ax, (5.48, 4.48), (6.53, 5.73), SLATE, both=True, rad=-.02, lw=1.6)
    label(ax, 6.04, 5.08, "USB · PBR1", 7.4, SLATE, True, ha="center")
    arrow(ax, (9.42, 5.73), (11.37, 5.73), SLATE, both=True, lw=1.6)
    label(ax, 10.39, 6.10, "Host 控制触点*", 7.1, SLATE, True, ha="center")
    arrow(ax, (5.48, 3.94), (11.37, 4.42), GREEN, both=True, rad=-.015, lw=1.8)
    label(ax, 8.45, 3.62, "认证 BLE 或固定热点 LAN（会话只选一种）", 7.4, GREEN, True, ha="center")
    arrow(ax, (11.38, 3.30), (5.48, 3.12), AMBER, rad=.025, lw=1.6)
    label(ax, 8.45, 3.48, "签名包 → 授权接收路径 → 验证后缓存", 7.4, AMBER, True, ha="center")
    arrow(ax, (3.16, 2.46), (3.16, 2.29), VIOLET, lw=1.4)
    label(ax, 8.0, 1.02, "* 触点的电气时序/引脚不在此示意中；以匹配的板卡固件和硬件图纸为准。", 7.4, MUTED, ha="center")
    save(fig, "01_system_architecture.png")


def draw_02():
    fig, ax = base(2, "控制面与数据面：两种职责，明确边界", "PBR1 报告连接和控制事件；认证 BLE / LAN 承载模块业务。Pogo 不承载连续业务流。",
                   "产品配置约束单一主会话：fixed_ble 或 fixed_hotspot_lan。图示为职责边界，不暗示双通道并发。")
    panel(ax, .68, 4.42, 14.62, 2.34, "A  /  控制面", "设备接入事件和受限控制命令", SLATE)
    card(ax, 1.02, 4.83, 3.25, 1.16, "模块 ↔ Host 控制触点", "唤醒 / 识别等板级控制\n具体电气细节依赖 Host 固件", SLATE, SLATE_BG, "module control")
    card(ax, 6.32, 4.83, 3.30, 1.16, "ESP32 Host / PBR1 桥", "控制帧：event · sequence · payload", SLATE, SLATE_BG, "control bridge")
    card(ax, 11.05, 4.83, 3.60, 1.16, "Pi PogoControlTransport", "USB serial → HostEventDetector\n→ ConnectionManager", BLUE, BLUE_BG, "pi runtime", title_size=10)
    arrow(ax, (4.30, 5.42), (6.29, 5.42), SLATE, both=True)
    arrow(ax, (9.65, 5.42), (11.02, 5.42), SLATE, both=True)
    pill(ax, 6.52, 4.50, 2.90, "无传感器业务 payload", SLATE, SLATE_BG)

    panel(ax, .68, 1.20, 14.62, 2.78, "B  /  数据面", "通过认证会话处理状态、命令、数据事件；模块包按显式策略接收。", GREEN)
    card(ax, 1.02, 1.76, 3.25, 1.22, "ESP32 模块业务", "module-transport-v1\n身份 · token · session_ready", GREEN, GREEN_BG, "module")
    card(ax, 6.30, 1.76, 3.37, 1.22, "一个选定的主通道", "BLE-only  或  Hotspot-LAN-only\n失败不静默切换另一种业务模式", GREEN, GREEN_BG, "authenticated lane", title_size=10)
    card(ax, 11.05, 1.76, 3.60, 1.22, "Pi 插件 C ABI", "send_data / on_data\n业务消息不走 PBR1", VIOLET, VIOLET_BG, "plugin")
    arrow(ax, (4.30, 2.48), (6.27, 2.48), GREEN, both=True)
    arrow(ax, (9.70, 2.48), (11.02, 2.48), VIOLET, both=True)
    label(ax, 5.28, 3.18, "BLE / LAN", 7.1, GREEN, True, ha="center")
    label(ax, 10.36, 3.18, "经校验后分发", 7.1, VIOLET, True, ha="center")
    pill(ax, 10.89, 1.38, 4.02, "插件包：active data lane；或显式 hotspot_lan 包策略", AMBER, AMBER_BG, 7)
    save(fig, "02_control_data_plane.png")


def draw_03():
    fig, ax = base(3, "从 attach 到 ready：连接成功不等于会话就绪", "ready 只有在模块已加载，且认证数据会话收到并验证 session_ready 后才成立。",
                   "Host 电气握手由板级固件负责；此图从 Pi 收到 PBR1 attach 事件开始，避免把未收录的固件细节画成事实。")
    stages = [
        ("01", "收到 attach", "PBR1 ControlEvent\n检查 event / module_id\n/ instance", SLATE, SLATE_BG),
        ("02", "验证并选策略", "检查 instance\nmodule-transport-v1\n凭据取自 Pi profile", BLUE, BLUE_BG),
        ("03", "建立固定通道", "fixed_ble → BLE\nfixed_hotspot_lan →\nPi AP / LAN", GREEN, GREEN_BG),
        ("04", "认证会话", "Pi 发送 session_open + token\n模块返回 session_ready", GREEN, GREEN_BG),
        ("05", "接收并验证插件", "active lane / 显式包路径\n签名 · 哈希 · 架构\nmanifest", AMBER, AMBER_BG),
        ("06", "用户信任并加载", "首次信任需确认\n加载 C ABI + QML 页面", VIOLET, VIOLET_BG),
    ]
    x0, gap, w, h = .78, .35, 2.23, 2.16
    y = 4.24
    for i, (n, title, body, c, fill) in enumerate(stages):
        x = x0 + i*(w+gap)
        rounded(ax, x, y, w, h, fill=fill, edge=LINE, radius=.16, lw=1.0, z=2)
        circ = Circle((x+.33, y+h-.39), .19, facecolor=c, edgecolor=c, zorder=3)
        ax.add_patch(circ); label(ax, x+.33, y+h-.39, n, 7.3, PAPER, True, ha="center")
        label(ax, x+.17, y+h-.83, title, 10.2, INK, True, va="top")
        label(ax, x+.17, y+.81, body, 8.15, MUTED, va="top", linespacing=1.5)
        if i < len(stages)-1:
            arrow(ax, (x+w+.025, y+1.07), (x+w+gap-.02, y+1.07), GREEN, lw=1.5)

    rounded(ax, 1.03, 2.43, 13.95, .87, fill=PAPER, edge=LINE, radius=.14, lw=1, z=1)
    label(ax, 1.30, 2.96, "READY", 8, GREEN, True)
    label(ax, 2.35, 2.96, "loaded plugin", 8.6, INK, True)
    label(ax, 5.05, 2.96, "+", 10, MUTED, True, ha="center")
    label(ax, 5.50, 2.96, "authenticated data session", 8.6, INK, True)
    label(ax, 10.18, 2.96, "session_ready checked", 8.1, GREEN, True)
    label(ax, 1.30, 2.65, "cards.ready = ready && loaded && !stopping", 8.2, MUTED)
    arrow(ax, (14.30, 4.17), (14.30, 3.38), VIOLET, "成功", (14.62, 3.77), lw=1.5)

    card(ax, 1.03, 1.15, 6.62, .82, "失败路径保留在状态 / error", "超时、身份或配置错误不会被伪报为 ready；数据断开进入 data_lost 并按固定模式重连。", RED, RED_BG, title_size=9.5, body_size=7.8)
    card(ax, 8.02, 1.15, 6.96, .82, "触点拔出不是无线断开", "BLE/LAN 若仍然有效，进入 contact_detached；数据会话单独判断。", SLATE, SLATE_BG, title_size=9.5, body_size=7.8)
    save(fig, "03_pogo_handshake.png")


def node(ax, x, y, w, h, title, color=BLUE, fill=BLUE_BG, size=8.5):
    rounded(ax, x, y, w, h, fill=fill, edge=color, radius=.15, lw=1.2, z=3)
    label(ax, x+w/2, y+h/2, title, size, INK, True, ha="center")


def draw_04():
    fig, ax = base(4, "模块会话状态机", "状态名称与迁移边来自 pi_terminal/src/core/SessionStateMachine.cpp。",
                   "图中突出常用路径，省略部分可达 error 边；不是所有错误都会自动恢复，也不代表硬件场景必然成功。")
    # Main acquisition chain.
    chain = [
        (.78, "attached", SLATE, SLATE_BG), (2.56, "connecting", BLUE, BLUE_BG),
        (4.42, "downloading\npackage", AMBER, AMBER_BG), (6.55, "verifying\npackage", AMBER, AMBER_BG),
        (8.70, "awaiting\ntrust", VIOLET, VIOLET_BG), (10.76, "loading\nplugin", VIOLET, VIOLET_BG),
        (12.90, "ready", GREEN, GREEN_BG)]
    y, w, h = 5.72, 1.48, .86
    for x, name, color, fill in chain:
        nice = name
        node(ax, x, y, w, h, nice, color, fill, 8.0)
    for i in range(len(chain)-1):
        x = chain[i][0]
        nxt = chain[i+1][0]
        arrow(ax, (x+w+.02,y+h/2),(nxt-.02,y+h/2),GREEN if i >= 2 else SLATE,lw=1.4)
    label(ax, 8.55, 6.78, "已信任包可跳过 awaiting trust", 7.8, MUTED, ha="center")

    # Contact-only transitions preserve the live data session.
    node(ax, 12.90, 4.14, w, h, "contact_detached", SLATE, SLATE_BG, 7.7)
    arrow(ax, (13.64,5.68),(13.64,5.02),SLATE,lw=1.45)
    label(ax,14.45,5.36,"触点离开",7.1,SLATE,True,ha="center")
    arrow(ax, (13.08,5.02),(13.08,5.68),GREEN,lw=1.45)
    label(ax,12.30,5.34,"触点恢复",7.1,GREEN,True,ha="center")
    label(ax, 14.14, 3.90, "无线仍活跃：保持加载", 7.0, MUTED, ha="center")

    # Loss and recovery row.
    node(ax, 9.15, 2.55, 1.75, .86, "data_lost", AMBER, AMBER_BG)
    arrow(ax,(12.92,5.68),(10.70,3.42),AMBER,both=True,rad=.19,lw=1.55)
    label(ax,10.72,4.20,"超时 / 恢复",7.0,AMBER,True,ha="center")
    arrow(ax, (9.36,3.42),(5.18,5.68),AMBER,rad=-.04,lw=1.35,style="--")
    label(ax,7.34,4.02,"需重收包",7.1,AMBER,True,ha="center")

    node(ax, .88, 2.55, 1.50, .86, "error", RED, RED_BG)
    arrow(ax, (2.72,5.68),(1.92,3.45),RED,rad=.12,lw=1.35)
    label(ax,2.02,4.60,"错误",7.1,RED,True,ha="center")
    arrow(ax, (2.22,3.45),(3.04,5.68),BLUE,rad=.12,lw=1.35,style="--")
    label(ax,3.14,4.42,"显式 retry",7.1,BLUE,True,ha="center")

    node(ax, 11.28, 1.25, 1.55, .78, "disconnecting", RED, RED_BG, 7.5)
    node(ax, 13.18, 1.25, 1.35, .78, "unloading", VIOLET, VIOLET_BG, 7.5)
    node(ax, 14.76, 1.25, .78, .78, "closed", SLATE, SLATE_BG, 7.2)
    arrow(ax,(13.28,5.70),(12.10,2.08),RED,rad=.12,lw=1.35)
    label(ax,13.09,3.48,"主动断开 / 停止",7.1,RED,True,ha="center")
    arrow(ax,(12.85,1.64),(13.15,1.64),VIOLET,lw=1.35)
    arrow(ax,(14.55,1.64),(14.74,1.64),SLATE,lw=1.35)
    save(fig, "04_state_machine.png")


def draw_05():
    fig, ax = base(5, "Pi 侧插件：先校验，再加载；先卸载页面，再释放库", "ESP32 提供/存储包；aarch64 .so 只在 Raspberry Pi 进程中执行。",
                   "同步卸载契约要求 stop() 返回前收束插件线程、定时器、回调和资源；可信插件与隔离沙箱不是同一件事。")
    panel(ax, .68, 4.08, 14.64, 2.68, "01  /  获取与启用", "正常路径先有 session_ready；包校验通过且满足信任策略后才创建插件。", GREEN)
    steps = [
        (.98, "认证会话", "session_ready\n身份 / token / seq", GREEN, GREEN_BG),
        (3.34, "接收包", "活动数据通道\n或显式包热点", BLUE, BLUE_BG),
        (5.70, "签名与内容校验", "Ed25519 descriptor\nSHA-256 / 文件清单", AMBER, AMBER_BG),
        (8.06, "信任决策", "首次确认\n或已信任密钥", VIOLET, VIOLET_BG),
        (10.42, "QLibrary / C ABI", "ABI / ELF / manifest\n同步停止契约", VIOLET, VIOLET_BG),
        (12.78, "挂载 QML", "页面 Loader 创建\n会话进入 ready", GREEN, GREEN_BG),
    ]
    for x, title, body, c, fill in steps:
        card(ax,x,4.58,2.02,1.55,title,body,c,fill,title_size=9.1,body_size=7.6)
    for i in range(len(steps)-1):
        arrow(ax,(steps[i][0]+2.04,5.34),(steps[i+1][0]-.03,5.34),GREEN if i==0 or i==4 else SLATE,lw=1.35)

    panel(ax, .68, 1.17, 14.64, 2.37, "02  /  有序卸载", "ConnectionManager 先关闭页面；ModuleManager 再停止插件并释放共享库。", RED)
    unload = [
        (1.05, "disconnecting", "停止数据发送"),
        (3.55, "卸载页面", "pageMustClose\nLoader 销毁完成"),
        (6.05, "on_detach()", "通知插件退出会话"),
        (8.55, "stop()", "同步结束后台工作"),
        (11.05, "destroy() → dlclose()", "实例销毁后释放库"),
    ]
    for x,title,body in unload:
        card(ax,x,1.68,2.20,1.18,title,body,RED if title.startswith("disconnect") else VIOLET,RED_BG if title.startswith("disconnect") else VIOLET_BG,title_size=8.7,body_size=7.6)
    for i in range(len(unload)-1):
        arrow(ax,(unload[i][0]+2.22,2.27),(unload[i+1][0]-.03,2.27),RED,lw=1.35)
    label(ax, 8.0, 3.82, "运行中：on_data / action 回调在 Host 线程串行分发；插件需保持非阻塞", 8.5, MUTED, ha="center")
    save(fig, "05_plugin_lifecycle.png")


def draw_06():
    fig, ax = base(6, "传输策略：会话通道与插件包通道分别决策", "module-transport-v1 固定一种业务通道；BLE 设备只有在显式要求时才借用热点下载包。",
                   "当前 Host 验证 fixed_ble / fixed_hotspot_lan 与 data / hotspot_lan；LAN 会话必须使用 active data lane。")
    panel(ax,.68,4.20,7.08,2.55,"A  /  模块业务会话", "module_config 是权威传输策略；Pi profile 提供本机凭据和发现配置。",GREEN)
    card(ax,1.02,4.70,2.42,1.28,"fixed_ble","业务只走认证 BLE\n不回退 LAN",GREEN,GREEN_BG,"mode")
    card(ax,4.92,4.70,2.42,1.28,"fixed_hotspot_lan","业务只走认证热点 LAN\n不回退 BLE",BLUE,BLUE_BG,"mode")
    pill(ax,1.02,4.34,6.32,"不支持 BLE + LAN 并行业务模式",RED,RED_BG)

    panel(ax,8.04,4.20,7.28,2.55,"B  /  插件包获取策略", "包通道是单独配置项；下载热点临时启用，不改变 BLE 业务会话。",AMBER)
    card(ax,8.38,4.70,2.90,1.28,"package_transport = data","通过当前已认证的\nBLE 或 LAN 主会话",GREEN,GREEN_BG,"默认 / active lane",title_size=8.8)
    card(ax,12.02,4.70,2.92,1.28,"package_transport = hotspot_lan","BLE-only 会话显式租用热点\n仅用于包下载",AMBER,AMBER_BG,"显式例外",title_size=8.6)

    rounded(ax,.92,1.15,14.20,2.45,fill=PAPER,edge=LINE,radius=.15,lw=1,z=1)
    label(ax,1.20,3.26,"可接受组合",10.5,INK,True)
    # Compact policy matrix.
    xs=[1.20,4.36,7.34,10.38,13.10]
    headers=["会话模式","包模式","传感器 / 业务","包来源","说明"]
    for x,t in zip(xs,headers): label(ax,x,2.90,t,8.0,MUTED,True)
    ax.plot([1.16,14.84],[2.68,2.68],color=LINE,lw=1)
    rows=[
        ("fixed_ble","data","认证 BLE","同一 BLE 链路","无需启热点"),
        ("fixed_ble","hotspot_lan","仍为 BLE","临时热点 HTTP","仅包通道借用热点"),
        ("fixed_hotspot_lan","data","认证 LAN","同一 LAN 链路","配置校验强制 data"),
    ]
    for r,row in enumerate(rows):
        yy=2.38-r*.40
        for x,t in zip(xs,row): label(ax,x,yy,t,7.8,GREEN if x==xs[0] else INK, x==xs[0])
    save(fig,"06_transport_selection.png")


def draw_07():
    fig, ax = base(7, "硬件与网络：角色连接图，不是针脚定义", "Host bridge 把控制事件接入 Pi；Pi 与功能模块之间另有认证无线数据会话。",
                   "本仓库缺少配套 Host 板卡针脚图。上电、GPIO、电压、DET 与 UART 排针接法必须以实际硬件原理图为准。")
    panel(ax,.72,1.43,14.56,5.38,"连接角色", "图中仅标出源码协议/系统职责可确认的逻辑链路。",SLATE)
    card(ax,1.10,4.35,3.27,1.27,"Raspberry Pi","Qt 终端 · USB serial\nPBR1 控制桥",BLUE,BLUE_BG,"host")
    card(ax,6.35,4.35,3.27,1.27,"ESP32 Host / Bridge","接收板级控制事件\n经 USB bridge 转发",SLATE,SLATE_BG,"control bridge")
    card(ax,11.60,4.35,3.27,1.27,"ESP32-S3 模块","控制固件 · 业务固件\n传感器 / 功能服务",GREEN,GREEN_BG,"module")
    arrow(ax,(4.40,4.99),(6.31,4.99),SLATE,"USB · PBR1",(5.36,5.27),both=True)
    arrow(ax,(9.66,4.99),(11.56,4.99),SLATE,"板级控制触点*",(10.62,5.27),both=True)

    rounded(ax,1.12,1.74,14.10,1.90,fill="#F8FAF6",edge=LINE,radius=.12,lw=1,z=2)
    label(ax,1.42,3.33,"无线业务通道（Pi ↔ 模块）",8.7,GREEN,True)
    card(ax,1.42,2.10,2.50,.70,"Pi BLE / WLAN","活动会话端点",BLUE,BLUE_BG,title_size=8.2,body_size=7.0)
    card(ax,12.08,2.10,2.50,.70,"模块 BLE / WLAN","活动会话端点",GREEN,GREEN_BG,title_size=8.2,body_size=7.0)
    arrow(ax,(3.96,2.45),(12.04,2.45),GREEN,both=True,lw=1.75)
    label(ax,8.0,2.78,"固定 BLE-only 或 Hotspot-LAN-only；认证后承载业务与插件包",7.6,GREEN,True,ha="center")
    label(ax,8.0,1.96,"仅 package_transport=hotspot_lan 时，BLE 业务会话才可临时租用热点下载包。",7.4,MUTED,ha="center")
    label(ax,8.0,1.70,"* “板级控制触点”不是接口针脚声明；不同板卡版本需单独核对接线与电平。",7.7,MUTED,ha="center")
    save(fig,"07_hardware_connection.png")


def draw_08():
    fig, ax = base(8, "签名插件包：分段接收、逐层验证、原子缓存", "校验成功只代表包满足当前验证条件；首次使用仍可能需要用户信任确认。",
                   "当前限制：数据通道压缩包 ≤ 1 MiB；包工具 HTTP 下载 ≤ 32 MiB；缓存上限 256 MiB。")
    # Source and explicit transport alternatives.
    card(ax,.78,5.27,2.42,1.18,"模块包来源","package.json\npackage.tar.zst",GREEN,GREEN_BG,"source",title_size=9.6)
    card(ax,4.05,6.00,3.18,.98,"认证 active data lane","PackageReceiver stop-and-wait\nJSON 分片 ≤ 512 B",BLUE,BLUE_BG,"BLE / LAN",title_size=9.0,body_size=7.5)
    card(ax,4.05,4.70,3.18,.98,"显式 hotspot_lan","仅包下载租用热点\nHTTP 包工具限量下载",AMBER,AMBER_BG,"package-only",title_size=9.0,body_size=7.5)
    arrow(ax,(3.22,5.86),(4.02,6.42),BLUE,rad=-.03)
    arrow(ax,(3.22,5.68),(4.02,5.18),AMBER,rad=.03)

    card(ax,7.92,5.29,3.02,1.20,"临时接收区","descriptor + 压缩包\n不在这里执行代码",SLATE,SLATE_BG,"staging",title_size=9.5)
    arrow(ax,(7.26,6.40),(7.89,5.99),BLUE)
    arrow(ax,(7.26,5.18),(7.89,5.68),AMBER)

    card(ax,11.68,5.04,3.56,1.65,"验证链", "1  Ed25519 签名验证\n2  SHA-256 / size / 文件清单\n3  safe path / archive bounds\n4  module_id / ABI / Qt / ELF 架构",AMBER,AMBER_BG,"package_tool",title_size=10.0,body_size=7.6)
    arrow(ax,(10.96,5.88),(11.65,5.88),GREEN,lw=1.5)

    card(ax,11.68,2.65,3.56,1.42,"内容寻址缓存","按包 SHA-256 原子落盘\n校验缓存文件集合与摘要\nLRU 总量上限 256 MiB",VIOLET,VIOLET_BG,"verified cache",title_size=9.6,body_size=7.8)
    arrow(ax,(13.44,5.00),(13.44,4.10),VIOLET,lw=1.5)

    card(ax,7.92,2.65,3.02,1.42,"信任门槛","首次签名者需确认\n或复用已信任 key + manifest",AMBER,AMBER_BG,"first-use trust",title_size=9.2,body_size=7.8)
    arrow(ax,(11.65,3.36),(10.97,3.36),VIOLET,lw=1.5)
    card(ax,4.05,2.65,3.18,1.42,"Pi 原生加载","QLibrary / C ABI\nmanifest 与内嵌信息一致\n注册对应 QML 页面",GREEN,GREEN_BG,"aarch64 host",title_size=9.2,body_size=7.8)
    arrow(ax,(7.89,3.36),(7.26,3.36),GREEN,lw=1.5)
    label(ax,2.00,3.32,"ESP32 只提供包；\n永不执行 Pi 的 .so",8.1,SLATE,True,ha="center",linespacing=1.5)
    arrow(ax,(4.02,3.36),(3.24,3.36),SLATE,lw=1.4,style="--")
    save(fig,"08_plugin_package_transfer.png")


def main():
    for draw in (draw_01, draw_02, draw_03, draw_04, draw_05, draw_06, draw_07, draw_08):
        draw()
    print(f"Rendered 8 source-checked figures into {FIG}")


from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn


DOCX = ROOT / "RaspberryPi_ESP32_Modular_Terminal_Technical_Spec.docx"


def _font(run, size=10, bold=False, color=INK, name="Noto Sans CJK SC"):
    run.font.name = name
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.color.rgb = RGBColor.from_string(color.lstrip("#"))
    rpr = run._element.get_or_add_rPr()
    fonts = rpr.rFonts
    if fonts is not None:
        fonts.set(qn("w:eastAsia"), name)
    return run


def _paragraph(doc, text="", style=None, size=10, bold=False, color=INK, after=5, before=0):
    p = doc.add_paragraph(style=style)
    p.paragraph_format.space_after = Pt(after)
    p.paragraph_format.space_before = Pt(before)
    p.paragraph_format.line_spacing = 1.12
    _font(p.add_run(text), size, bold, color)
    return p


def _heading(doc, text, level=1):
    p = doc.add_paragraph(style=f"Heading {level}")
    p.paragraph_format.keep_with_next = True
    p.paragraph_format.space_before = Pt(12 if level == 1 else 7)
    p.paragraph_format.space_after = Pt(5)
    _font(p.add_run(text), 17 if level == 1 else 12, True, GREEN if level == 1 else BLUE)
    return p


def _bullet(doc, text):
    p = doc.add_paragraph(style="List Bullet")
    p.paragraph_format.space_after = Pt(3)
    p.paragraph_format.line_spacing = 1.1
    _font(p.add_run(text), 9.5, False, INK)
    return p


def _cell(cell, text, header=False):
    cell.text = ""
    cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
    p = cell.paragraphs[0]
    p.paragraph_format.space_after = Pt(2)
    p.paragraph_format.space_before = Pt(2)
    _font(p.add_run(str(text)), 8.5, header, PAPER if header else INK)
    if header:
        props = cell._tc.get_or_add_tcPr()
        shade = OxmlElement("w:shd")
        shade.set(qn("w:fill"), GREEN.lstrip("#"))
        props.append(shade)


def _table(doc, headers, rows):
    table = doc.add_table(rows=1, cols=len(headers))
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.style = "Table Grid"
    for cell, text in zip(table.rows[0].cells, headers):
        _cell(cell, text, True)
    for row in rows:
        cells = table.add_row().cells
        for cell, text in zip(cells, row):
            _cell(cell, text)
    doc.add_paragraph().paragraph_format.space_after = Pt(1)
    return table


def _figure(doc, number, caption, explanation):
    path = FIG / (f"{number:02d}_" + {
        1: "system_architecture.png", 2: "control_data_plane.png",
        3: "pogo_handshake.png", 4: "state_machine.png",
        5: "plugin_lifecycle.png", 6: "transport_selection.png",
        7: "hardware_connection.png", 8: "plugin_package_transfer.png",
    }[number])
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(3)
    p.paragraph_format.space_after = Pt(2)
    p.add_run().add_picture(str(path), width=Inches(6.75))
    cap = doc.add_paragraph()
    cap.alignment = WD_ALIGN_PARAGRAPH.CENTER
    cap.paragraph_format.space_after = Pt(3)
    _font(cap.add_run(f"图 {number}  ·  {caption}"), 9, True, GREEN)
    _paragraph(doc, explanation, size=9, color=MUTED, after=7)


def _page_number(paragraph):
    paragraph.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    _font(paragraph.add_run("PI TERMINAL   ·   CURRENT SOURCE   ·   "), 8, False, MUTED)
    field = OxmlElement("w:fldSimple")
    field.set(qn("w:instr"), "PAGE")
    paragraph._p.append(field)


def build_document():
    doc = Document()
    section = doc.sections[0]
    section.top_margin = Inches(.62)
    section.bottom_margin = Inches(.62)
    section.left_margin = Inches(.72)
    section.right_margin = Inches(.72)
    section.header_distance = Inches(.30)
    section.footer_distance = Inches(.30)

    normal = doc.styles["Normal"]
    normal.font.name = "Noto Sans CJK SC"
    normal.font.size = Pt(10)
    normal.font.color.rgb = RGBColor.from_string(INK.lstrip("#"))
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), "Noto Sans CJK SC")
    for style_name in ("List Bullet", "List Number"):
        style = doc.styles[style_name]
        style.font.name = "Noto Sans CJK SC"
        style.font.size = Pt(9.5)
        style._element.rPr.rFonts.set(qn("w:eastAsia"), "Noto Sans CJK SC")
    header = section.header.paragraphs[0]
    _font(header.add_run("MULTI-FUNCTION ESP32  /  PI TERMINAL"), 8, True, GREEN)
    _page_number(section.footer.paragraphs[0])
    doc.core_properties.title = "Pi Terminal 模块系统技术说明（当前源码版）"
    doc.core_properties.subject = "依据当前主线源码整理的架构、会话、传输、插件和包验证说明"
    doc.core_properties.author = "Multi-Function ESP32 · Pi Terminal"
    doc.core_properties.keywords = "Pi Terminal, ESP32, PBR1, BLE, Hotspot LAN, C ABI"

    # Cover
    _paragraph(doc, "CURRENT IMPLEMENTATION  /  2026-10-24", size=9, bold=True, color=GREEN, after=14, before=35)
    _paragraph(doc, "树莓派 + ESP32", size=28, bold=True, color=INK, after=0)
    _paragraph(doc, "模块终端技术说明", size=27, bold=True, color=INK, after=10)
    _paragraph(doc, "控制面 · 认证数据会话 · Pi 侧 C ABI 插件", size=13, color=BLUE, after=15)
    p = doc.add_paragraph()
    p.paragraph_format.space_after = Pt(12)
    p.add_run().add_picture(str(FIG / "01_system_architecture.png"), width=Inches(6.75))
    _paragraph(doc, "本文以当前 Git 主线的 Pi 终端源码及版本化 module-devkit SDK 为依据。", size=11, color=MUTED, after=8)
    _paragraph(doc, "范围说明", size=12, bold=True, color=GREEN, after=4)
    _bullet(doc, "图中的控制桥、BLE/LAN 会话和插件加载流程对应仓库可见的代码；不把早期设计提案、维护者本地文件或历史实机记录冒充为当前实现。")
    _bullet(doc, "Host 板级固件和完整电气图纸不在当前 Git 主线中，因此不推测触点针脚、唤醒时序、电平或硬件已验收状态。")
    _bullet(doc, "此文档用于解释软件架构与代码边界，不等同于设备安装手册、产品安全认证或实机验收报告。")
    doc.add_page_break()

    _heading(doc, "1. 总体架构", 1)
    _paragraph(doc, "Pi 终端承担 UI、连接会话、插件验证及插件生命周期；ESP32 Host/控制桥把板级控制事件接入 Pi；ESP32-S3 模块承担设备侧业务。模块插件是运行在 Raspberry Pi 进程中的 aarch64 C ABI 共享库，不在 ESP32 上执行。")
    _table(doc, ["部分", "当前职责", "源码入口"], [
        ("Pi 应用 / QML", "Qt Quick 页面与应用交互", "pi_terminal/qml/ · src/app/"),
        ("连接运行时", "PBR1 控制桥、模块配置、BLE/LAN 会话、状态机", "pi_terminal/src/core/ · src/transport/ · src/protocol/"),
        ("内置共享库", "terminal_system / terminal_music / terminal_ui；随主程序同版发布", "pi_terminal/CMakeLists.txt"),
        ("外接插件", "manifest 校验、QLibrary、C ABI 回调与 QML 页面", "pi_terminal/src/core/ModuleManager.cpp"),
        ("模块 SDK", "固件组件、模板、包工具和智能盆栽示例", "module-devkit/sdk/ · examples/smart-planter/"),
    ])
    _figure(doc, 1, "模块系统架构", "逻辑图分开表示控制桥、无线业务通道和 Pi 侧插件；Host 板级电气实现按硬件版本另行确认。")

    doc.add_page_break()
    _heading(doc, "2. 控制面与数据面", 1)
    _paragraph(doc, "控制面使用 PBR1 over USB serial 传递 Host 控制事件和受限命令。PBR1 的代码类型包含 ControlEvent、ControlCommand 和 Error；插件业务数据不通过 PBR1。")
    _paragraph(doc, "数据面由模块配置固定为 BLE-only 或 fixed_hotspot_lan。连接后以 module_id、module_instance、version、sequence 和 token 校验 session_open/session_ready；数据链路失败时不会静默切换到另一种业务模式。")
    _figure(doc, 2, "控制面与数据面边界", "Pogo/Host 控制事件和认证无线业务是两条不同责任链。仅在显式包传输策略下，BLE 业务会话才会短时租用热点下载插件包。")

    doc.add_page_break()
    _heading(doc, "3. attach 到 ready", 1)
    _paragraph(doc, "Pi 收到并验证 PBR1 attach 事件后，ConnectionManager 读取模块提供的 module-transport-v1 配置，并与 Pi 的本机预置 profile 合并：模块配置决定数据/包通道，本机 profile 提供 token、BLE 发现信息、网络端点和信任公钥等本地信息。")
    _paragraph(doc, "Pi 通过唯一选定的数据通道发送 session_open。收到模块的 session_ready 后，Pi 验证身份、token、所选传输及序号；插件包随后按策略接收、签名验证并经过信任决策。只有插件成功加载且数据会话就绪，模块卡片才报告 ready。")
    _figure(doc, 3, "从 attach 到 ready", "本图从 Pi 的 PBR1 attach 事件开始；板级触点如何唤醒/握手由未随主线提供的 Host 固件负责，不在图中臆造。")

    doc.add_page_break()
    _heading(doc, "4. 会话状态与恢复", 1)
    _paragraph(doc, "SessionStateMachine 将回调请求串行投递到状态机事件队列。attached 之后进入 connecting；会话认证后可能进入包接收、签名验证、信任等待和插件加载；ready 表示数据会话及插件都可用。")
    _paragraph(doc, "触点离开不必然结束无线业务：ready 可转入 contact_detached，并在触点恢复后回 ready。无线数据超时进入 data_lost；恢复结果取决于插件是否已加载以及是否需要重新接收包。最终关闭顺序经过 disconnecting → unloading → closed。")
    _figure(doc, 4, "模块会话状态机", "图中突出常用边；源代码中还存在其他 error 和恢复迁移。状态图不是硬件成功保证。")

    doc.add_page_break()
    _heading(doc, "5. 插件加载与同步卸载", 1)
    _paragraph(doc, "模块包安装与插件加载由不同步骤完成。包工具在 GUI 进程外检查签名、包摘要、文件清单、相对路径、架构及 manifest；首次信任者可能需要用户确认。ModuleManager 随后加载共享库并核对导出函数、ABI 和内嵌 manifest，再注册插件页面。")
    _paragraph(doc, "插件通过纯 C ABI 与 Host 通信；C++/Qt 类型不作为跨库 ABI。回调在 Host 线程串行分发，插件不得阻塞 UI。共享库与主程序处于同一进程，当前设计并不提供插件沙箱隔离。")
    _figure(doc, 5, "Pi 侧插件生命周期", "关闭页面 Loader 后再依序调用 on_detach()、stop()、destroy()，最后释放 QLibrary；同步停止契约要求 stop() 返回后插件工作线程和回调均已结束。")

    doc.add_page_break()
    _heading(doc, "6. 传输策略", 1)
    _paragraph(doc, "当前 module-transport-v1 仅接受 fixed_ble 或 fixed_hotspot_lan。fixed_hotspot_lan 会话的 package_transport 必须是 data。BLE-only 会话可选 data（默认活动链路）或显式 hotspot_lan（只为插件包临时启用热点），不是双无线业务会话。")
    _table(doc, ["会话模式", "包模式", "业务会话", "插件包通道"], [
        ("fixed_ble", "data", "认证 BLE", "同一 BLE 会话"),
        ("fixed_ble", "hotspot_lan", "仍为认证 BLE", "显式短期热点 HTTP"),
        ("fixed_hotspot_lan", "data", "认证热点 LAN", "同一 LAN 会话"),
    ])
    _figure(doc, 6, "会话通道与包通道", "连接策略与包获取策略分别判断；不能将旧的 BLE + LAN 并行草案当作当前配置。")

    doc.add_page_break()
    _heading(doc, "7. 硬件与网络逻辑连接", 1)
    _paragraph(doc, "源码可确认 Pi 的 PogoControlTransport 通过 USB serial 与 PBR1 桥通信；Host 与模块之间的物理控制触点属于板级接口；Pi 与功能模块还会建立 BLE 或热点 LAN 数据会话。")
    _figure(doc, 7, "逻辑角色连接图", "此图不是原理图或接线针脚图。当前仓库没有提供足以确认实际 Host 板版本的 GPIO、DET、UART、供电电平或连接器 pinout。接线前必须查目标板卡的正式原理图。")

    doc.add_page_break()
    _heading(doc, "8. 插件包接收与验证", 1)
    _paragraph(doc, "数据通道 PackageReceiver 使用有界 stop-and-wait 分片暂存 package.json 与 package.tar.zst，再调用 package_tool。工具验证 Ed25519 签名描述符、压缩包 SHA-256/长度、文件级摘要、归档路径和展开上限；检查 module_id、ABI、目标 ELF 架构、Qt 主版本、文件入口和同步卸载契约。通过后按摘要原子落入有界缓存。")
    _table(doc, ["限制", "当前实现"], [
        ("数据通道 package.json", "≤ 256 KiB"),
        ("数据通道压缩包", "≤ 1 MiB"),
        ("包工具 HTTP 下载", "≤ 32 MiB"),
        ("验证包缓存总量", "≤ 256 MiB，按使用时间回收旧缓存"),
        ("数据通道分片 payload", "单片 ≤ 512 B"),
    ])
    _figure(doc, 8, "签名插件包验证与缓存", "签名验证通过不等于插件已被用户信任；首次使用可能需要权限确认。ESP32 只提供包，aarch64 .so 仅在 Pi 上加载。")

    doc.add_page_break()
    _heading(doc, "9. 当前范围与验证边界", 1)
    _bullet(doc, "当前 GitHub 主线没有顶层 esp/ 正式固件目录。模块侧以版本化 module-devkit SDK 与智能盆栽示例为可审查来源；Host 板级握手细节不能仅凭早期图或本地历史记录推定。")
    _bullet(doc, "PBR1/USB 控制桥、BLE/LAN 模块会话和插件包接收是不同链路；串口打开、attach 到达或无线连接建立都不单独等于 session_ready。")
    _bullet(doc, "插件运行在 Pi 主程序进程并继承相同 OS 权限。manifest permissions 是能力声明，不是内核权限隔离；不可信插件需要独立进程和 IPC 才能建立隔离边界。")
    _bullet(doc, "本文和图表根据源码整理；本次文档更新没有重新进行固件烧录、Pi 实机联调、物理触点测试或重新验收所有旧测试记录。")
    _paragraph(doc, "源码索引：pi_terminal/src/core/ConnectionManager.cpp · TransportPolicy.h · SessionStateMachine.cpp · ModuleManager.cpp · PackageReceiver.cpp；module-devkit/tools/package_tool.py；module-devkit/sdk/include/pi_module_api.h。",
               size=8.5, color=MUTED, after=3)

    doc.save(DOCX)
    print(f"Wrote current-source technical document: {DOCX}")


def main():
    for draw in (draw_01, draw_02, draw_03, draw_04, draw_05, draw_06, draw_07, draw_08):
        draw()
    build_document()


if __name__ == "__main__":
    main()
