from pathlib import Path
from docx import Document
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_CELL_VERTICAL_ALIGNMENT
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch

ROOT = Path(__file__).resolve().parent
FIG = ROOT / "figures"
FIG.mkdir(exist_ok=True)
DOCX = ROOT / "RaspberryPi_ESP32_Modular_Terminal_Technical_Spec.docx"

BLUE = '#1f4e79'
TEAL = '#0f766e'
ORANGE = '#c2410c'
PURPLE = '#6d28d9'
GREEN = '#166534'
GREY = '#475569'


def fig_base(title, size=(13, 7)):
    fig, ax = plt.subplots(figsize=size)
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 7)
    ax.axis('off')
    ax.set_title(title, fontsize=17, fontweight='bold', color='#0f172a', pad=14)
    return fig, ax


def box(ax, x, y, w, h, text, color=BLUE, fill='#eff6ff', fs=10):
    patch = FancyBboxPatch((x, y), w, h, boxstyle='round,pad=0.04,rounding_size=0.08',
                           linewidth=1.7, edgecolor=color, facecolor=fill)
    ax.add_patch(patch)
    ax.text(x + w / 2, y + h / 2, text, ha='center', va='center', fontsize=fs, color='#0f172a')


def arrow(ax, start, end, label='', color=GREY):
    ax.add_patch(FancyArrowPatch(start, end, arrowstyle='-|>', mutation_scale=14,
                                 linewidth=1.4, color=color))
    if label:
        ax.text((start[0] + end[0]) / 2, (start[1] + end[1]) / 2 + 0.12,
                label, ha='center', va='center', fontsize=8.5, color=color)


def save(fig, name):
    fig.savefig(FIG / name, dpi=170, bbox_inches='tight')
    plt.close(fig)


def make_figures():
    fig, ax = fig_base('System Architecture')
    box(ax, .4, 2.6, 2.3, 1.5, 'Raspberry Pi\nQt 6 / QML\nModuleManager', BLUE, fs=11)
    box(ax, 3.3, 2.6, 2.3, 1.5, 'Generic .so plugin\nUI + business logic\nBLE/LAN adapter', PURPLE, '#f5f3ff', 10)
    box(ax, 6.2, 4.5, 2.3, 1.1, 'BLE\nsmall data', TEAL, '#ecfdf5', 11)
    box(ax, 6.2, 1.1, 2.3, 1.1, 'Pi Wi-Fi AP\nLAN bulk data', ORANGE, '#fff7ed', 10)
    box(ax, 9.3, 2.6, 2.3, 1.5, 'External ESP32\nhardware + real-time\nBLE / LAN service', GREEN, '#f0fdf4', 10)
    box(ax, 6.2, 5.9, 2.3, .55, 'ESP32 Host: Pogo control handshake', GREY, '#f1f5f9', 8)
    arrow(ax, (2.7, 3.35), (3.3, 3.35), 'load/unload', BLUE)
    arrow(ax, (5.6, 3.5), (6.2, 5.05), 'small data', TEAL)
    arrow(ax, (5.6, 3.0), (6.2, 1.65), 'bulk data', ORANGE)
    arrow(ax, (8.5, 5.05), (9.3, 3.6), 'BLE', TEAL)
    arrow(ax, (8.5, 1.65), (9.3, 3.1), 'Wi-Fi LAN', ORANGE)
    arrow(ax, (7.35, 5.9), (7.35, 5.65), 'Pogo', GREY)
    save(fig, '01_system_architecture.png')

    fig, ax = fig_base('Control Plane and Data Plane')
    box(ax, .6, 4.1, 2.0, .9, 'Pogo control\nUART / GPIO', GREY, '#f1f5f9')
    box(ax, 3.2, 4.1, 2.0, .9, 'ESP32 Host\nwake + handshake', BLUE)
    box(ax, 5.8, 4.1, 2.0, .9, 'USB control bridge\nCONTROL_EVENT', PURPLE, '#f5f3ff', 9)
    box(ax, 8.4, 4.1, 2.7, .9, 'Pi ConnectionManager\nload + negotiate', BLUE, '#eff6ff', 9)
    arrow(ax, (2.6, 4.55), (3.2, 4.55), 'handshake')
    arrow(ax, (5.2, 4.55), (5.8, 4.55), 'event')
    arrow(ax, (7.8, 4.55), (8.4, 4.55), 'control')
    ax.plot([.6, 11.1], [3.25, 3.25], color='#cbd5e1', linewidth=2)
    ax.text(5.85, 3.0, 'Control plane ends here; Pogo carries no business payload', ha='center', fontsize=10, color=GREY)
    box(ax, 1.3, 1.0, 2.4, 1.0, 'BLE transport\nstatus/config/small result', TEAL, '#ecfdf5', 9)
    box(ax, 4.6, 1.0, 2.4, 1.0, 'Hotspot LAN\nstream/bulk payload', ORANGE, '#fff7ed', 9)
    box(ax, 7.9, 1.0, 2.4, 1.0, '.so plugin\nonData()', PURPLE, '#f5f3ff', 10)
    arrow(ax, (9.75, 4.1), (2.5, 2.0), 'start BLE', TEAL)
    arrow(ax, (9.75, 4.1), (5.8, 2.0), 'start AP/LAN', ORANGE)
    arrow(ax, (3.7, 1.5), (7.9, 1.5), 'business data', TEAL)
    arrow(ax, (7.0, 1.5), (7.9, 1.5), '', ORANGE)
    save(fig, '02_control_data_plane.png')

    fig, ax = fig_base('Pogo Handshake Flow')
    steps = [
        ('1', 'Attach', 'detect contact +\nwake module'),
        ('2', 'Handshake', 'version + module_id\nunique device ID'),
        ('3', 'Fixed config', 'BLE name/UUID\nWi-Fi SSID/port'),
        ('4', 'Attach event', 'Host -> USB bridge\n-> Pi'),
        ('5', 'Direct connect', 'fixed BLE or\nfixed Wi-Fi config'),
        ('6', 'Idle contact', 'no business\npayload on Pogo'),
    ]
    for i, (num, title, desc) in enumerate(steps):
        x = .3 + i * 1.95
        color = BLUE if i < 4 else TEAL if i == 4 else GREY
        fill = '#eff6ff' if i < 4 else '#ecfdf5' if i == 4 else '#f1f5f9'
        box(ax, x, 3.0, 1.55, 1.35, f'{num}\n{title}\n{desc}', color, fill, 8.5)
        if i < 5:
            arrow(ax, (x + 1.55, 3.68), (x + 1.95, 3.68))
    ax.text(6, 1.55, 'Pogo is control plane only: wake -> identify -> fixed-config connect -> release',
            ha='center', fontsize=12, fontweight='bold', color='#0f172a')
    save(fig, '03_pogo_handshake.png')

    fig, ax = fig_base('Connection State Machine')
    states = [
        (.5, 5.2, 'HostReady'), (2.3, 5.2, 'Waking'), (4.1, 5.2, 'Identifying'),
        (6.0, 5.2, 'Negotiating'), (7.9, 5.2, 'Loading .so'), (9.8, 5.2, 'Connecting'),
        (9.8, 3.1, 'Streaming'), (7.0, 1.3, 'Detached'), (4.0, 1.3, 'Detaching'),
        (1.2, 1.3, 'Unloading')]
    for x, y, text in states:
        fill = '#f0fdf4' if text == 'Streaming' else '#eff6ff'
        box(ax, x, y, 1.45, .75, text, GREEN if text == 'Streaming' else BLUE, fill, 9)
    transitions = [
        ((1.95, 5.58), (2.3, 5.58), 'attach'), ((3.75, 5.58), (4.1, 5.58), 'wake'),
        ((5.55, 5.58), (6.0, 5.58), 'info'), ((7.45, 5.58), (7.9, 5.58), 'select'),
        ((9.35, 5.58), (9.8, 5.58), 'load'), ((10.52, 5.2), (10.52, 3.85), 'connect'),
        ((9.8, 3.1), (8.45, 1.95), 'contact out'), ((7.0, 1.68), (5.45, 1.68), 'data lost'),
        ((4.0, 1.68), (2.65, 1.68), 'destroy'), ((10.52, 3.85), (10.52, 5.2), 'restore')]
    for start, end, label in transitions:
        arrow(ax, start, end, label, TEAL if label == 'restore' else GREY)
    ax.text(8.2, 2.65, 'BLE/LAN alive: keep plugin', ha='center', fontsize=9, color=GREY)
    save(fig, '04_state_machine.png')

    fig, ax = fig_base('Dynamic Plugin Lifecycle')
    participants = [('ESP32 Host', .8), ('ConnectionManager', 3.0), ('ModuleManager', 5.4), ('.so plugin', 7.8), ('QML/UI', 10.2)]
    for name, x in participants:
        ax.text(x, 6.4, name, ha='center', fontweight='bold', fontsize=10, color='#0f172a')
        ax.plot([x, x], [.7, 6.15], color='#cbd5e1', linestyle='--')
    events = [
        (.8, 3.0, 5.7, 'handshake + module_id'), (3.0, 5.4, 5.1, 'load + validate'),
        (5.4, 7.8, 4.5, 'dlopen + create'), (7.8, 10.2, 3.9, 'register page/actions'),
        (3.0, 7.8, 3.1, 'onAttach(transport_info)'), (3.0, 7.8, 2.5, 'onData(BLE/LAN)'),
        (3.0, 7.8, 1.9, 'onDetach + stop'), (7.8, 10.2, 1.3, 'destroy QML'),
        (5.4, 7.8, .75, 'destroy + dlclose')]
    for x1, x2, y, label in events:
        arrow(ax, (x1, y), (x2, y), label, BLUE if y > 3.5 else TEAL if y > 2 else ORANGE)
    save(fig, '05_plugin_lifecycle.png')

    fig, ax = fig_base('Fixed BLE / Hotspot LAN Configuration')
    box(ax, .5, 2.7, 2.0, 1.0, 'manifest +\nproduct config', BLUE, fs=10)
    box(ax, 3.2, 4.3, 2.2, 1.0, 'fixed_ble', TEAL, '#ecfdf5', 10)
    box(ax, 3.2, 2.7, 2.2, 1.0, 'fixed_hotspot_lan', ORANGE, '#fff7ed', 10)
    box(ax, 3.2, 1.1, 2.2, 1.0, 'fixed_ble_and_lan', PURPLE, '#f5f3ff', 10)
    box(ax, 6.4, 4.3, 2.0, 1.0, 'BleTransport\nfixed name/UUID', TEAL, '#ecfdf5', 9)
    box(ax, 6.4, 2.7, 2.0, 1.0, 'HotspotLanTransport\nfixed SSID/port', ORANGE, '#fff7ed', 9)
    box(ax, 6.4, 1.1, 2.0, 1.0, 'BLE control +\nLAN bulk data', PURPLE, '#f5f3ff', 9)
    box(ax, 9.4, 2.7, 2.0, 1.0, 'plugin onData()\none business path', PURPLE, '#f5f3ff', 9)
    arrow(ax, (2.5, 3.2), (3.2, 4.8), '', TEAL)
    arrow(ax, (2.5, 3.2), (3.2, 3.2), '', ORANGE)
    arrow(ax, (2.5, 3.2), (3.2, 1.6), '', PURPLE)
    arrow(ax, (5.4, 4.8), (6.4, 4.8), '', TEAL)
    arrow(ax, (5.4, 3.2), (6.4, 3.2), '', ORANGE)
    arrow(ax, (5.4, 1.6), (6.4, 1.6), '', PURPLE)
    arrow(ax, (8.4, 4.8), (9.4, 3.2), 'small data', TEAL)
    arrow(ax, (8.4, 3.2), (9.4, 3.2), 'bulk', ORANGE)
    arrow(ax, (8.4, 1.6), (9.4, 3.2), 'both', PURPLE)
    save(fig, '06_transport_selection.png')

    fig, ax = fig_base('Hardware and Network Wiring')
    box(ax, .6, 2.4, 2.4, 1.2, 'Raspberry Pi 4B/5\nUSB + Wi-Fi AP', BLUE, fs=11)
    box(ax, 4.2, 2.4, 2.4, 1.2, 'ESP32 Host\ncontrol bridge', GREY, '#f1f5f9', 11)
    box(ax, 7.8, 2.4, 2.4, 1.2, 'ESP32 module\nBLE / Wi-Fi', GREEN, '#f0fdf4', 11)
    arrow(ax, (3.0, 3.0), (4.2, 3.0), 'USB-UART/CDC\ncontrol events', BLUE)
    arrow(ax, (6.6, 3.0), (7.8, 3.0), 'Pogo\nlow-speed handshake', GREY)
    arrow(ax, (1.8, 2.4), (8.6, 1.0), 'BLE small data / hotspot LAN bulk data', TEAL)
    ax.text(6, 4.8, 'Pogo: 3.3 V TTL, TX/RX crossed, shared GND\nNo continuous business stream on contact pins',
            ha='center', fontsize=11, color='#0f172a')
    save(fig, '07_hardware_connection.png')

    fig, ax = fig_base('Module Plugin Package Transfer and Loading', (13, 7))
    box(ax, .4, 2.8, 2.2, 1.35, 'ESP32 module storage\nplugin.tar.zst\nmanifest + .so + QML', GREEN, '#f0fdf4', 9)
    box(ax, 3.25, 4.65, 2.2, 1.1, 'Pogo control\npackage_id / hash / ABI', GREY, '#f1f5f9', 9)
    box(ax, 3.25, 1.15, 2.2, 1.1, 'BLE chunks or\nHotspot LAN download', TEAL, '#ecfdf5', 9)
    box(ax, 6.15, 2.8, 2.2, 1.35, 'Pi incoming temp\nlength + SHA-256\nsignature verification', BLUE, '#eff6ff', 9)
    box(ax, 9.0, 2.8, 2.2, 1.35, 'Pi package cache\n/version/arm64\natomic install', PURPLE, '#f5f3ff', 9)
    box(ax, 9.0, 5.25, 2.2, .85, 'dlopen()\nQML + QObject + actions', ORANGE, '#fff7ed', 9)
    arrow(ax, (2.6, 3.48), (3.25, 2.0), 'package bytes', TEAL)
    arrow(ax, (2.6, 3.65), (3.25, 5.2), 'control metadata', GREY)
    arrow(ax, (5.45, 2.0), (6.15, 3.48), 'download', TEAL)
    arrow(ax, (8.35, 3.48), (9.0, 3.48), 'verified package', PURPLE)
    arrow(ax, (10.1, 4.15), (10.1, 5.25), 'load instance', ORANGE)
    ax.text(6, .45, '.so is never executed on ESP32; only the Raspberry Pi loads it. Session unload uses dlclose(), verified cache may remain.', ha='center', fontsize=10, color='#0f172a')
    save(fig, '08_plugin_package_transfer.png')


def set_cell(cell, text, bold=False, color=None):
    cell.text = ''
    p = cell.paragraphs[0]
    r = p.add_run(str(text))
    r.bold = bold
    if color:
        r.font.color.rgb = RGBColor(*color)
    cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER


def shade(cell, fill):
    tcPr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement('w:shd')
    shd.set(qn('w:fill'), fill)
    tcPr.append(shd)


def add_table(doc, headers, rows, widths=None):
    table = doc.add_table(rows=1, cols=len(headers))
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    table.style = 'Table Grid'
    for i, h in enumerate(headers):
        set_cell(table.rows[0].cells[i], h, True, (255, 255, 255))
        shade(table.rows[0].cells[i], '1F4E79')
    for row in rows:
        cells = table.add_row().cells
        for i, value in enumerate(row):
            set_cell(cells[i], value)
            if len(table.rows) % 2 == 0:
                shade(cells[i], 'F8FAFC')
    if widths:
        for row in table.rows:
            for i, width in enumerate(widths):
                row.cells[i].width = Inches(width)
    doc.add_paragraph()
    return table


def add_code(doc, text):
    p = doc.add_paragraph()
    p.style = 'No Spacing'
    p.paragraph_format.left_indent = Inches(.25)
    p.paragraph_format.right_indent = Inches(.25)
    for line in text.strip('\n').splitlines():
        r = p.add_run(line + '\n')
        r.font.name = 'DejaVu Sans Mono'
        r.font.size = Pt(8.5)
        r.font.color.rgb = RGBColor(31, 41, 55)
    return p


def add_bullets(doc, items, level=0):
    for item in items:
        p = doc.add_paragraph(style='List Bullet' if level == 0 else 'List Bullet 2')
        p.add_run(item)


def add_fig(doc, filename, caption):
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.add_run().add_picture(str(FIG / filename), width=Inches(6.65))
    c = doc.add_paragraph(caption)
    c.alignment = WD_ALIGN_PARAGRAPH.CENTER
    c.runs[0].italic = True
    c.runs[0].font.size = Pt(9)


def heading(doc, text, level=1):
    doc.add_heading(text, level=level)


def build_doc():
    doc = Document()
    sec = doc.sections[0]
    sec.top_margin = Inches(.65)
    sec.bottom_margin = Inches(.65)
    sec.left_margin = Inches(.75)
    sec.right_margin = Inches(.75)
    styles = doc.styles
    styles['Normal'].font.name = 'Microsoft YaHei'
    styles['Normal'].font.size = Pt(10.5)
    styles['Title'].font.name = 'Microsoft YaHei'
    styles['Heading 1'].font.name = 'Microsoft YaHei'
    styles['Heading 2'].font.name = 'Microsoft YaHei'
    styles['Heading 3'].font.name = 'Microsoft YaHei'
    styles['Heading 1'].font.color.rgb = RGBColor(31, 78, 121)
    styles['Heading 2'].font.color.rgb = RGBColor(15, 118, 110)

    header = sec.header.paragraphs[0]
    header.text = 'Raspberry Pi + ESP32 通用动态模块终端技术文档'
    header.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    header.runs[0].font.size = Pt(8)
    footer = sec.footer.paragraphs[0]
    footer.text = '技术规格 · 通用插件架构 · 控制面/数据面分离'
    footer.alignment = WD_ALIGN_PARAGRAPH.CENTER
    footer.runs[0].font.size = Pt(8)

    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.add_run('树莓派 + ESP32\n').bold = True
    p.runs[0].font.size = Pt(25)
    p.runs[0].font.color.rgb = RGBColor(31, 78, 121)
    r = p.add_run('通用动态模块终端技术规格书')
    r.bold = True; r.font.size = Pt(21); r.font.color.rgb = RGBColor(15, 118, 110)
    p2 = doc.add_paragraph()
    p2.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p2.add_run('控制面：Pogo 触点握手  ·  数据面：BLE / 树莓派热点 LAN  ·  功能面：动态 .so 插件').font.size = Pt(11)
    doc.add_paragraph()
    meta = add_table(doc, ['项目属性', '内容'], [
        ('文档定位', '树莓派通用动态模块终端产品技术规格与 ESP32 通信移植方案'),
        ('目标平台', 'Raspberry Pi 4B / Raspberry Pi 5，Raspberry Pi OS aarch64'),
        ('软件栈', 'C++17、Qt 6/QML、CMake、BlueZ/QtBluetooth、QtNetwork'),
        ('模块形态', '每个功能模块 = manifest.json + 独立 .so + QML/资源/业务代码'),
        ('通信原则', 'Pogo 只握手；BLE 传小数据；树莓派热点 LAN 传大数据'),
        ('文档版本', '统一审查版 1.0'),
    ], [1.5, 5.0])
    doc.add_page_break()

    heading(doc, '目录', 1)
    add_bullets(doc, [
        '1. 架构结论与设计边界', '2. 系统总体架构', '3. 通信与协议边界',
        '4. 通用动态插件模型', '4.2 模块接入提示与主界面功能卡片', '5. manifest 与 C ABI',
        '6. BLE 与热点 LAN 数据链路', '7. 动态加载、状态机与销毁',
        '8. 硬件连接与网络职责', '9. Qt/QML UI 与适老化要求',
        '10. 工程目录、构建与部署', '11. 与当前 ESP32 项目的契合度与复用矩阵',
        '12. ESP32 与树莓派改动清单', '13. 最终契合度与可行性审查',
        '14. 测试矩阵与验收标准', '15. 安全边界与权限',
        '16. 风险与待确认项', '附录 A：现有项目代码复用边界',
    ])

    heading(doc, '1. 架构结论与设计边界', 1)
    doc.add_paragraph('本方案将终端设计为与具体传感器无关的通用运行时。终端不内置热成像、环境、功率、相机、血压或 GSM 等业务；所有具体功能通过独立 .so 插件提供。')
    add_bullets(doc, [
        'Pogo 触点只承担设备唤醒、低速握手和接入确认，不承担业务数据流；BLE 名称/UUID、Wi-Fi SSID 和端口采用固定产品配置。',
        'BLE 用于状态、配置、控制命令和小型结果；大数据和连续采样由树莓派开启热点后通过局域网承载。',
        '树莓派主程序负责通用运行时、动态库生命周期、QML 宿主、BLE 中心、Wi-Fi AP、LAN 会话和日志。',
        'ESP32 模块负责硬件驱动、实时采集、BLE GATT 服务以及局域网客户端/服务端。',
        '模块连接时动态加载 .so；数据会话结束时按顺序停止、注销、销毁并 dlclose。',
    ])
    add_fig(doc, '01_system_architecture.png', '图 1：总体架构——Pogo 控制面与 BLE/LAN 数据面分离')

    heading(doc, '2. 系统总体架构', 1)
    doc.add_paragraph('系统由四类实体组成：树莓派终端、ESP32 Host、外部 ESP32 模块以及树莓派侧动态插件。ESP32 Host 可以由现有 C6 Host 工程裁剪得到，但只保留 Pogo 唤醒和接入确认功能。模块的 BLE 名称、UUID、热点 SSID 和端口采用固定产品配置，不做复杂运行时协商。')
    add_table(doc, ['实体', '职责', '不负责的内容'], [
        ('Raspberry Pi 主程序', '模块扫描、manifest 校验、.so 加载、BLE、Wi-Fi AP、LAN、QML 宿主、日志', '不实现具体传感器驱动'),
        ('ESP32 Host', '唤醒模块、Pogo 握手、读取设备信息、向 Pi 转发控制事件', '不承载树莓派业务数据；不负责 BLE/LAN 会话状态'),
        ('外部 ESP32 模块', '硬件采集、实时任务、BLE GATT、LAN 数据服务', '不负责终端 UI'),
        ('树莓派 .so 插件', 'QML 页面、数据模型、业务协议、功能入口、设备操作', '不直接访问其他插件或主程序单例'),
    ], [1.4, 2.9, 2.2])
    add_fig(doc, '02_control_data_plane.png', '图 2：控制面与数据面分层')

    heading(doc, '3. 通信与协议边界', 1)
    heading(doc, '3.1 Pogo 控制面', 2)
    doc.add_paragraph('Pogo 控制面使用触点 UART 或控制 GPIO 完成低速握手。握手完成后触点进入空闲；树莓派按固定 BLE 名称/UUID 或固定 Wi-Fi SSID 直接连接，不切换到持续业务数据的 2 Mbps UART，也不使用 ESP-NOW 作为本终端的数据面。')
    add_fig(doc, '03_pogo_handshake.png', '图 3：触点握手流程')
    doc.add_paragraph('当前项目的 linkit_lnk1_packet_t 只包含 magic、module_type、MAC 和 protocol_id。固定配置模式不要求把 BLE 名称、BLE UUID、LAN 服务名或热点 SSID 放进 Pogo 握手包；这些参数由模块固件、插件包 manifest 和终端产品配置预先约定。')
    heading(doc, '3.2 PiBridge 控制桥帧', 2)
    add_code(doc, 'PBR1 | direction | kind | seq32 | length16 | payload | crc16')
    add_bullets(doc, [
        '字段统一使用小端序。',
        'length 最大为 2048 字节，超过上限直接丢弃。',
        'CRC 使用 CRC-16/CCITT-FALSE，初值 0xFFFF，覆盖 direction 到 payload。',
        'kind 只定义 CONTROL_EVENT、CONTROL_COMMAND 和 ERROR。',
        '解析器必须在坏长度、坏 CRC、随机字节和超时后从下一个 PBR1 重新同步。',
    ])
    heading(doc, '3.3 控制事件 JSON', 2)
    add_code(doc, '''{
  "event": "attach",
  "module_id": 128,
  "module_instance": "01HZX...",
  "transport_options": ["ble", "hotspot_lan"],
  "ble_name": "MODULE_EXAMPLE",
  "ble_service_uuid": "00000000-0000-0000-0000-000000000001",
  "lan_service": "_example._tcp",
  "lan_port": 4580,
  "timestamp_ms": 123456
}''')

    heading(doc, '4. 通用动态插件模型', 1)
    doc.add_paragraph('每个功能插件是一个独立安装单元。主程序只识别通用元数据和 ABI，不知道插件的具体业务类型。')
    add_code(doc, '''my_module/
├── manifest.json
├── libmy_module.so
└── assets/
    ├── qml/MyPage.qml
    └── icons/''')
    add_table(doc, ['manifest 字段', '用途'], [
        ('id', '插件字符串唯一 ID'), ('module_id', '设备线上 ID，必须唯一'),
        ('capabilities', '业务能力字符串数组'), ('qml_page', '插件页面入口'),
        ('actions', '主界面动态功能入口'), ('transports', 'ble / hotspot_lan'),
        ('control_protocol / data_protocol', '控制面和业务数据协议版本'),
        ('permissions', '插件能力声明；不是 Linux 安全隔离'),
        ('abi_version / min_host_api', 'ABI 和宿主兼容性校验'),
    ], [2.3, 4.2])

    heading(doc, '4.2 模块接入提示与主界面功能卡片', 2)
    doc.add_paragraph('模块接入全程向用户显示状态，正式功能卡片只在插件包校验成功、.so 加载成功并完成数据连接后出现。')
    add_table(doc, ['状态', '提示', '卡片行为'], [
        ('attached', '检测到模块，正在唤醒', '灰色进度卡片'),
        ('connecting', '正在连接模块', '显示固定 BLE/Wi-Fi 连接进度'),
        ('downloading_package', '正在下载模块功能包', '显示插件包下载进度'),
        ('verifying_package', '正在验证模块功能', '显示 hash/签名校验'),
        ('loading_plugin', '正在准备功能界面', '显示 dlopen/QML 注册状态'),
        ('ready', '模块已连接', '显示正式功能卡片'),
        ('data_lost', '连接暂时中断，正在重连', '保留卡片并显示黄色状态'),
        ('detached', '模块已断开', '淡出并删除卡片'),
        ('error', '模块连接失败', '显示重试/关闭操作'),
    ], [1.8, 2.4, 2.7])
    add_code(doc, '''ModuleCardModel {
    module_id;
    module_instance;
    title;
    icon;
    status;
    progress;
    page_url;
    actions;
}''')
    doc.add_paragraph('卡片由主程序统一管理，不能直接持有已经 dlclose() 的插件对象。模块断开时先禁用交互，再销毁 QML/插件实例，最后执行淡出删除。')

    heading(doc, '4.1 插件内部 C++ 接口', 2)
    add_code(doc, '''struct TransportInfo {
    QStringList available;
    QString bleName;
    QString bleServiceUuid;
    QString lanService;
    quint16 lanPort = 0;
};

struct DisconnectRequest {
    QString reason;
    bool stopRequired = true;
    bool autoReconnect = false;
};

class IModulePlugin {
public:
    virtual ~IModulePlugin() = default;
    virtual QString id() const = 0;
    virtual quint8 wireModuleId() const = 0;
    virtual QVariantMap capabilities() const = 0;
    virtual QUrl qmlPage() const = 0;
    virtual QVariantList actions() const = 0;
    virtual bool onAttach(const TransportInfo&) = 0;
    virtual void onTransportConnected(uint8_t transport) = 0;
    virtual void onData(quint8 transport, const QByteArray&) = 0;
    virtual void onDisconnectRequest(const DisconnectRequest&) = 0;
    virtual void onDetach() = 0;
    virtual void stop() = 0;
};''')
    doc.add_paragraph('该 C++ 类只作为插件内部实现接口。跨动态库边界必须使用纯 C ABI，不得导出 QString、QUrl、QVariantMap、QByteArray 或 C++ 类。')

    heading(doc, '4.1 模块携带 .so 插件包的传输模型', 2)
    doc.add_paragraph('本方案中 .so 封装在功能模块内部。ESP32 只存储和提供插件包，不执行 Linux/ARM64 .so；树莓派使用固定 BLE/Wi-Fi 配置连接模块，完整插件包优先通过热点 LAN 下载，完成校验后才动态加载。')
    add_fig(doc, '08_plugin_package_transfer.png', '图 8：模块携带插件包、树莓派接收校验并动态加载')
    add_code(doc, '''模块 plugin.tar.zst
    -> package_id / version / size / SHA-256 / signature
    -> BLE chunks 或 Hotspot LAN download
    -> Pi incoming 临时目录
    -> SHA-256 + signature + ABI + host_arch + module_id 校验
    -> /var/lib/pi-terminal/modules/<module_id>/<version>/
    -> dlopen() -> QML + QObject + actions
    -> 会话结束：销毁实例 + dlclose()；已验证缓存可保留''')
    doc.add_paragraph('当前多个 ESP32-S3 固件为 2 MB Flash，通常不足以保存完整 ARM64 Qt .so、QML 和资源。需要更大 Flash、外部 QSPI、SPI NAND 或 SD 卡等模块存储方案。')

    heading(doc, '4.2 模块主动断开与实际可行性', 2)
    doc.add_paragraph('模块按钮或模块状态可以通过 BLE/LAN 发送 module_disconnect_request。树莓派返回 disconnect_ack 后，统一停止数据传输、调用 onDetach/stop、移除页面、销毁插件并 dlclose。该请求不通过 Pogo 发送，插件不能自行卸载动态库。')
    add_code(doc, '''{
  "event": "module_disconnect_request",
  "module_id": 128,
  "module_instance": "01HZX...",
  "reason": "user_button",
  "stop_required": true,
  "auto_reconnect": false
}

{
  "event": "disconnect_ack",
  "module_id": 128,
  "module_instance": "01HZX...",
  "accepted": true
}''')
    add_bullets(doc, [
        'ESP32 只存储和传输 ARM64 插件包，不能执行树莓派 .so。',
        '完整插件包不应默认通过 BLE 传输；非小包应使用树莓派热点 LAN。',
        '当前 2 MB Flash 模块通常不足以保存完整 Qt/QML ARM64 插件包，需要额外模块存储。',
        '.so 必须针对 Raspberry Pi OS aarch64 编译，不能依赖 ESP-IDF 或 ESP32 ELF。',
        '只有完成 hash、签名、ABI、架构、module_id 和版本校验后才能 dlopen。',
    ])

    heading(doc, '5. manifest 与 C ABI', 1)
    add_code(doc, '''{
  "id": "example.device",
  "name": "示例功能模块",
  "version": "1.0.0",
  "module_id": 128,
  "package_id": "example.device-1.0.0-arm64",
  "package_version": "1.0.0",
  "host_arch": ["aarch64"],
  "package_size": 524288,
  "package_sha256": "...",
  "package_signature": "...",
  "control_protocol": "pogo-control-v1",
  "data_protocol": "module-defined-v1",
  "library": "libexample_device.so",
  "qml_page": "qrc:/example/ExamplePage.qml",
  "capabilities": ["sample.read", "sample.configure"],
  "actions": ["open", "start", "stop"],
  "available_transports": ["ble"],
  "package_transport": "hotspot_lan",
  "ble": {"device_name": "MODULE_EXAMPLE", "service_uuid": "00000000-0000-0000-0000-000000000001"},
  "package_url": "http://192.168.50.2:4581",
  "abi_version": 1,
  "min_host_api": 1
}''')
    add_code(doc, '''typedef struct pi_host_services pi_host_services_t;
typedef struct pi_module_handle pi_module_handle_t;

uint32_t pi_module_api_version(void);
const char *pi_module_manifest_json(void);
pi_module_handle_t *pi_module_create(const pi_host_services_t *host);
void pi_module_destroy(pi_module_handle_t *module);
int pi_module_on_data(pi_module_handle_t *, uint8_t transport,
                      const uint8_t *payload, uint32_t length);
int pi_module_on_attach(pi_module_handle_t *, const char *transport_json);
void pi_module_on_transport_connected(pi_module_handle_t *, uint8_t transport);
const char *pi_module_qml_page(pi_module_handle_t *);''')
    add_code(doc, '''typedef struct pi_host_services {
    uint32_t api_version;
    void *context;
    int (*send_control)(void *context, uint8_t command,
                        const uint8_t *payload, uint16_t length,
                        uint16_t *sequence_out);
    int (*send_data)(void *context, uint8_t transport,
                     const uint8_t *payload, uint32_t length);
    void (*publish_action)(const char *action_id, const char *text);
    void (*publish_event)(const char *type, const char *json);
    const char *(*data_directory)();
    void (*log)(int level, const char *message);
} pi_host_services_t;''')

    heading(doc, '6. BLE 与热点 LAN 数据链路', 1)
    heading(doc, '6.1 BLE 小数据', 2)
    add_bullets(doc, ['用于状态、配置、控制命令和小型结果。', '插件使用固定设备名称、Service UUID 和特征 UUID 完成扫描、服务发现、读写和通知订阅。', '建议单次应用 payload 不超过 512 字节；更大内容使用 message_id、fragment_index、fragment_count 和 CRC 分片。', 'BLE 断开后进入重连状态，超过重试窗口再报告 session_disconnected。'])
    heading(doc, '6.2 树莓派热点 LAN 大数据', 2)
    add_bullets(doc, ['树莓派启动固定 SSID 的受控 Wi-Fi AP，负责认证、DHCP、服务发现和连接生命周期。', '模块使用固定 SSID/认证配置加入热点，默认通过固定端口 TCP 传输连续数据；QUIC 作为后续可选方案。', 'LAN 帧包含协议版本、module_id、module_instance、序号、长度、payload 和 CRC。', '热点配置使用预共享密钥；第一版固定配置简单，但量产时仍需避免所有模块使用相同密钥。'])
    add_fig(doc, '06_transport_selection.png', '图 4：BLE/LAN 传输选择流程')
    add_table(doc, ['条件', '选择'], [
        ('只支持 BLE', 'BleTransport'), ('只支持 hotspot_lan', '启动树莓派 AP + HotspotLanTransport'),
        ('两者都支持，小数据', 'BLE'), ('两者都支持，高速/连续/大 payload', '热点 LAN'),
        ('LAN 已承载业务流', 'BLE 仅保留状态和断线通知，不重复发送业务数据'),
    ], [3.6, 2.9])

    heading(doc, '7. 动态加载、状态机与销毁', 1)
    add_fig(doc, '05_plugin_lifecycle.png', '图 5：插件动态加载、数据运行和卸载时序')
    add_fig(doc, '04_state_machine.png', '图 6：连接状态机')
    doc.add_paragraph('卸载顺序必须固定：标记 Stopping → 停止 BLE/LAN 业务传输 → onDetach() → 移除 action → 销毁 QML 页面和 queued callback → stop() → 等待线程/定时器/文件描述符结束 → pi_module_destroy() → dlclose()。')
    doc.add_paragraph('触点拔出不等于数据会话结束。如果 BLE/LAN 仍可用，插件保持加载并进入 Detached；只有数据链路丢失、重连超时或用户主动断开时才卸载插件。')

    heading(doc, '8. 硬件连接与网络职责', 1)
    add_fig(doc, '07_hardware_connection.png', '图 7：硬件连接和数据网络')
    add_bullets(doc, ['树莓派 4B/5 优先通过 USB 连接 ESP32 Host，避免占用系统 UART。', 'Pogo UART/控制线使用 3.3 V TTL，不能连接 RS-232 电平。', 'TX/RX 交叉，唤醒/DET 与 GND 连接，所有板卡可靠共地。', 'Pogo 只使用低速握手波特率，不切换为持续业务数据的 2 Mbps 链路。', '模块 BLE UUID、LAN 服务名和端口由 manifest/握手信息提供。'])
    add_table(doc, ['链路', '方向', '用途'], [
        ('Pogo UART/GPIO', 'Host ↔ 模块', '唤醒、低速握手、接入确认'),
        ('USB-UART/CDC', 'Host ↔ Pi', '控制桥事件和控制命令'),
        ('BLE', 'Pi ↔ 模块', '小数据、状态、配置、控制'),
        ('Wi-Fi AP + LAN', 'Pi ↔ 模块', '大数据和连续数据流'),
    ], [1.8, 1.8, 2.9])

    heading(doc, '9. Qt/QML UI 与适老化要求', 1)
    add_bullets(doc, ['Qt 6 + QML 负责界面，C++ 负责运行时、插件和传输抽象。', '模块连接后由 manifest/actions 动态注入主界面；断开后带淡出动画移除。', '页面通过统一 PageProvider 挂载，插件可以提供自己的 QML 页面和数据模型。', '主界面使用车机风格卡片、水平滑动、Z 轴旋转和 GPU 模糊。', '背景模糊采用 FBO + 降采样 mipmap 链，禁止 QGraphicsBlurEffect。', '正文 ≥18pt、标题 ≥24pt、点按热区 ≥48dp、控件间距 ≥16dp、WCAG AA 对比度 ≥4.5:1。', '关键操作支持 TTS，避免只用红绿区分状态。'])
    add_code(doc, '''渲染管线：
主场景 -> 抓帧到 FBO -> 逐级 1/2 降采样 -> mipmap 链
        -> 向上混合 -> 背景/卡片模糊层
实现：QQuickFramebufferObject + Scene Graph；禁止 QGraphicsBlurEffect''')

    heading(doc, '10. 工程目录、构建与部署', 1)
    add_code(doc, '''pi_terminal/
├── CMakeLists.txt
├── terminal/main.cpp
├── core/
│   ├── ModuleManager.{h,cpp}
│   ├── ModuleInstance.{h,cpp}
│   ├── ConnectionManager.{h,cpp}
│   └── DataRouter.{h,cpp}
├── transport/
│   ├── PogoControlTransport.{h,cpp}
│   ├── BleTransport.{h,cpp}
│   └── HotspotLanTransport.{h,cpp}
├── protocol/
│   ├── pogo_handshake_codec.{h,cpp}
│   └── control_bridge_protocol.h
├── modules/api/
├── ui/qml/
└── tools/{mock_pogo_control,mock_data_transport}''')
    add_bullets(doc, ['本机构建：Qt 6、C++17、CMake。', '目标构建：Raspberry Pi OS aarch64 交叉编译。', '运行时插件目录：/usr/lib/pi-terminal/modules/<module-id>/。', '插件目录内放置 manifest.json、.so 和 assets。', '第一阶段只加载本地可信插件，并校验目录权限、文件所有者和可选签名。'])

    heading(doc, '11. 与当前 ESP32 项目的契合度与复用矩阵', 1)
    doc.add_paragraph('结论：当前项目的模块硬件代码高度可复用，但 LinkIt 持续数据传输、C6/LVGL 宿主 UI 和 ESP-NOW 业务链路不能直接移植到树莓派通用终端。')
    add_table(doc, ['当前项目内容', '复用等级', '复用与修改结论'], [
        ('instrument_protocol.h', '高', '复用 module_id、命令语义、状态码和数据结构；业务 payload 需要封装到 BLE/LAN。'),
        ('linkit_handshake.h / handshake.c', '中', '复用 WHO?/LNK1/ACK、MAC、protocol_id 和超时重试；固定模式使用预置 BLE/SSID，动态模式才需要 device_info 扩展。'),
        ('linkit_frame.h / CRC', '低到中', '可复用校验和序号思路；不作为 BLE/LAN 业务协议，可选用于控制桥内层。'),
        ('linkit_transport_uart/espnow', '低/不复用', 'UART 只保留低速控制；ESP-NOW 不作为本方案的树莓派数据面。'),
        ('ESP32 采集任务与传感器驱动', '高', '保留 GPIO、SPI、I2C、Pico、MLX90640、INA228、环境和陀螺仪采集；替换 linkit_write_frame() 上行。'),
        ('C6 instrument_runtime', '中', '复用 module_id 分发和统计思路；改写为 Qt/C++ DataRouter。'),
        ('C6/S3 LVGL UI', '低', '只参考页面字段和交互逻辑；必须重写为 Qt 6/QML。'),
        ('wireless_dap / CMSIS-DAP', '不属于本方案', '作为原项目独立调试器保留，不纳入通用模块数据面。'),
        ('ESP-IDF/FreeRTOS/CMake', '仅 ESP32 侧', '继续构建 ESP32 固件；树莓派 .so 使用 Qt 6/C++17/CMake。'),
    ], [1.9, 1.0, 3.6])
    add_code(doc, '''现有 ESP32 任务：
linkit_frame_t out = { ... };
linkit_write_frame(&out, timeout);

新模块上行：
module_data_frame_t out = { ... };
ble_service_notify_or_lan_send(&out, sizeof(out));''')
    add_bullets(doc, [
        'ESP32 模块和产品配置固定 module_id、BLE 名称/UUID、Wi-Fi SSID、LAN 端口和 capabilities。',
        'ESP32 模块新增 BLE GATT service 或 LAN TCP service。',
        '树莓派 .so 不链接 ESP-IDF、FreeRTOS、LVGL，也不调用 linkit_write_frame()。',
        '固定配置模式不要求把 BLE/LAN 参数放进 linkit_lnk1_packet_t；只有未来做动态发现时才需要 device_info 扩展。',
    ])

    heading(doc, '12. ESP32 与树莓派改动清单', 1)
    add_table(doc, ['部分', '改动'], [
        ('ESP32 Host', '从现有 C6 Host/LinkIt 工程拆出无屏控制固件，保留低速握手、唤醒、设备信息和控制事件转发。'),
        ('ESP32 模块', '新增 BLE GATT 服务和/或热点 LAN 客户端/服务端；业务协议由模块插件共同定义。'),
        ('树莓派控制层', '实现 PogoControlTransport、控制事件 CRC、串口重连、有界队列和 module_instance 校验。'),
        ('树莓派数据层', '实现 BleTransport、HotspotLanTransport、Wi-Fi AP、DHCP、发现和 TCP 会话。'),
        ('插件层', '每个 .so 提供 QML 页面、C++ 数据模型、业务协议和 action；不直接访问 GPIO 或其他插件。'),
    ], [1.7, 5.0])

    heading(doc, '13. 最终契合度与可行性审查', 1)
    doc.add_paragraph('当前 ESP32 项目可以作为模块硬件固件和采集基础，但不能直接作为树莓派 BLE/LAN 动态插件终端运行。')
    add_table(doc, ['能力/要求', '当前状态', '结论'], [
        ('ESP32 采集驱动/实时任务', '已实现', '可直接复用；替换 linkit_write_frame 上行'),
        ('module_id/仪器命令', '已定义', '复用 instrument_protocol.h，业务通过 BLE/LAN'),
        ('Pogo 低速握手', 'WHO?/LNK1/ACK 已实现', '小改复用；取消 2 Mbps 业务链路'),
        ('固定 BLE GATT', '当前未实现', '必须新增固定名称/UUID 的 BLE service'),
        ('固定 Wi-Fi/LAN', '当前为 ESP-NOW/STA 信道逻辑', '必须新增树莓派 AP、固定 SSID STA 和 TCP service'),
        ('模块插件包存储', '2 MB Flash 通常不足', '硬件阻塞；需要更大/外部存储'),
        ('插件包下载', '当前无文件服务', '新增 LAN 文件服务，BLE 只做小包/元数据'),
        ('ARM64 .so', '当前只有 ESP32 ELF', '新增 Qt6/C++17/aarch64 SDK和交叉编译'),
        ('动态卡片/UI', '当前 C6 使用 LVGL 固定页面', '必须重写 ModuleCardModel + QML PageProvider'),
        ('模块主动断开', '当前无统一事件', '新增 module_disconnect_request/disconnect_ack'),
        ('CMSIS-DAP/ESP-NOW', '原项目独立功能', '保留原产品，不纳入通用模块数据面'),
    ], [2.2, 1.6, 2.9])
    add_bullets(doc, [
        '架构可行：是；当前项目直接运行：否。',
        'ESP32 采集代码复用：是；BLE/LAN 和插件包服务：必须新增。',
        '模块存储、ARM64 插件 SDK、Qt ABI 校验和签名校验是正式实现的前置条件。',
        '如果不增加模块存储、不新增 BLE/LAN 服务，系统只能继续作为原 C6/S3 LinkIt 仪器工程。',
    ])

    heading(doc, '14. 测试矩阵与验收标准', 1)
    add_table(doc, ['测试项', '验收标准'], [
        ('动态加载', 'manifest、ABI、module_id 不匹配时拒绝加载；未知 module_id 不加载任意库。'),
        ('动态卸载', '100 次连接/断开无重复页面、线程泄漏、queued signal 崩溃。'),
        ('触点握手', '唤醒、module_id、module_instance 和 attach 事件连续成功 100 次。'),
        ('BLE', '读写、通知、分片、CRC、重连正确。'),
        ('热点 LAN', 'AP、DHCP、发现、TCP 数据流和断线恢复正确。'),
        ('触点拔出', 'BLE/LAN 保持时插件不卸载；数据链路失败后按策略重连或卸载。'),
        ('控制协议', '坏长度、坏 CRC、随机字节后能从下一个 PBR1 恢复。'),
        ('UI 性能', 'Pi 4 目标 30 FPS；接收和数据处理不阻塞 UI 线程。'),
    ], [1.7, 5.0])

    heading(doc, '15. 安全边界与权限', 1)
    doc.add_paragraph('manifest.json 中的 permissions 只是能力声明，不等于 Linux 安全隔离。普通 dlopen() 加载的 .so 与终端主程序拥有相同进程权限，可以访问主程序能够访问的文件、设备和网络。')
    add_bullets(doc, [
        '第一阶段只加载本地可信插件，校验安装目录、文件所有者和可选签名。',
        '如果未来支持第三方或不可信插件，必须改为独立 helper process，通过 IPC 提供 HostServices。',
        'helper process 应配合 systemd sandbox、用户权限、设备白名单和资源限制。',
        '不能仅依赖 manifest.permissions 实现安全隔离。',
    ])

    heading(doc, '16. 风险与待确认项', 1)
    add_bullets(doc, ['IMX766、QtWebEngine 和 GPU FBO 属于产品层功能，应与通用模块运行时分开验证。', 'Qt 6 的动态 QML 资源注册/注销必须制作最小示例验证，确认 dlclose 前没有残留 QObject 或 queued signal。', '树莓派热点与 BLE 共存时需要验证信道、功耗、连接恢复和设备发现时序。', '普通 dlopen 插件与主程序拥有相同 Linux 权限；若加载不可信插件，必须改为独立 helper process + IPC + systemd sandbox。', '现有仓库的 LinkIt/ESP-NOW 持续数据代码属于原始 ESP32 仪器工程，不能直接当作本方案 BLE/LAN 数据实现。'])

    heading(doc, '附录 A：现有项目代码复用边界', 1)
    add_table(doc, ['现有代码/目录', '在新方案中的用途'], [
        ('shared/linkit_sdk', '复用 WHO?/LNK1/ACK、设备类型和 MAC 交换等握手思路；不复用持续业务帧作为 Pi 数据面。'),
        ('shared/instrument_protocol', '可作为现有仪器插件的业务协议样例；终端核心不内置这些业务结构。'),
        ('C6_Screen', '裁剪 LinkIt Host 握手和控制事件转发，去掉原有屏幕 UI 和持续数据展示。'),
        ('S3_* / C3_Gyro', '作为外部 ESP32 模块固件和插件联调样例；未来业务模块可完全不同。'),
        ('wireless_dap / ESP-NOW', '保留原项目调试器用途；不作为本终端 BLE/LAN 业务数据面。'),
    ], [2.2, 4.5])

    doc.add_paragraph()
    p = doc.add_paragraph('文档审查结论：')
    p.runs[0].bold = True
    doc.add_paragraph('两份 Markdown 技术文档已合并为本 Word 文档。最终架构统一为“Pogo 控制握手、BLE 小数据、树莓派热点 LAN 大数据、.so 动态插件”。')
    doc.save(DOCX)


if __name__ == '__main__':
    make_figures()
    build_doc()
    print(DOCX)
