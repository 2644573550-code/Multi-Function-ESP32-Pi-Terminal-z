# 生成设定好参数的"分件打印 3MF":预摆姿态 + 落床 + 居中(+ 拼盘)
import os, sys, struct, zipfile, math
import numpy as np
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
SRC = r"C:\Users\SomaRia\Documents\deepseek-harness\default-workspace\host-shell"
OUT = r"C:\Users\SomaRia\Desktop\pj\小爱风格主机外壳_打印"
BED = 256.0

# 零件: (文件名, 中文名, 绕X旋转角度, 姿态说明)
PARTS = [
    ("Shell_Lower", "下半部外壳", 0.0, "立着打(底面朝下),需支撑+5mm裙边"),
    ("Shell_Upper", "上半部外壳", 180.0, "翻转 180 度(屏幕面朝下),需少量支撑"),
    ("Front_Cover", "正面面板", 90.0, "外面朝下平躺,免支撑"),
    ("Back_Cover", "背板", -90.0, "外面朝下平躺(派支架朝上),免支撑"),
    ("Bottom_Cover", "底面盖板", 0.0, "外面朝下平躺,免支撑"),
    ("Channel_Cover", "走线槽盖", 125.0, "盖面朝下平躺,免支撑"),
    ("Shell_Lower_A", "下壳A段(z0~75)", 0.0, "原姿态立着打,需支撑"),
    ("Shell_Lower_B", "下壳B段(z75~150)", 180.0, "翻转180度,用平整分型面贴床,支撑大减"),
]
PLATES = [
    ("拼盘A_正面面板+走线槽盖", ["Front_Cover", "Channel_Cover"]),
    ("拼盘B_背板+底面盖板", ["Back_Cover", "Bottom_Cover"]),
    ("拼盘C_下半部外壳+上半部外壳", ["Shell_Lower", "Shell_Upper"]),
    ("拼盘D_下壳A段+下壳B段", ["Shell_Lower_A", "Shell_Lower_B"]),
]
GAP = 5.0        # 拼盘内两件的间隙(两个外壳 125+5+125 = 255,刚好放进 256 床)


def read_stl(p):
    with open(p, "rb") as f:
        f.read(80)
        n = struct.unpack("<I", f.read(4))[0]
        raw = f.read(n * 50)
    v = np.frombuffer(raw, dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))
    return v["v"].reshape(n, 3, 3).astype(np.float64)


def orient(T, deg):
    if abs(deg) < 1e-9:
        return T.copy()
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return T @ np.array([[1, 0, 0], [0, c, -s], [0, s, c]]).T


def place(T, dx, dy):
    """落到床面 z=0,并整体平移(dx,dy)"""
    T = T.copy()
    mn = T.reshape(-1, 3).min(axis=0)
    mx = T.reshape(-1, 3).max(axis=0)
    T[:, :, 0] += dx - (mn[0] + mx[0]) / 2.0
    T[:, :, 1] += dy - (mn[1] + mx[1]) / 2.0
    T[:, :, 2] -= mn[2]
    return T


def mesh_xml(T):
    verts, idx = {}, []
    for t in T:
        row = []
        for p in t:
            k = (round(p[0], 4), round(p[1], 4), round(p[2], 4))
            if k not in verts:
                verts[k] = len(verts)
            row.append(verts[k])
        idx.append(row)
    vl = ["<vertices>"] + ['<vertex x="%.4f" y="%.4f" z="%.4f"/>' % tuple(v) for v in verts] + ["</vertices>"]
    tl = ["<triangles>"] + ['<triangle v1="%d" v2="%d" v3="%d"/>' % tuple(r) for r in idx] + ["</triangles>"]
    return "\n".join(vl) + "\n" + "\n".join(tl)


CT = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
      ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
      ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n</Types>\n')
RL = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
      ' <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>\n')


def write_3mf(path, objs, title, note):
    res, items = [], []
    for i, (mesh, name) in enumerate(objs, start=1):
        res.append('  <object id="%d" type="model" name="%s"><mesh>\n%s\n  </mesh></object>' % (i, name, mesh))
        items.append('  <item objectid="%d"/>' % i)
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="zh-CN" '
             'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">\n'
             ' <metadata name="Application">DeepSeek-Harness</metadata>\n'
             ' <metadata name="Title">%s</metadata>\n <metadata name="Description">%s</metadata>\n'
             ' <resources>\n%s\n </resources>\n <build>\n%s\n </build>\n</model>\n' % (
                 title, note, "\n".join(res), "\n".join(items)))
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CT)
        z.writestr("_rels/.rels", RL)
        z.writestr("3D/3dmodel.model", model)


cache = {}
print("=== 分件打印 3MF(已摆好姿态、落床、居中于 256x256)===")
for i, (fn, cn, deg, note) in enumerate(PARTS, start=1):
    T = place(orient(read_stl(os.path.join(SRC, fn + ".stl")), deg), BED / 2, BED / 2)
    cache[fn] = T
    dim = T.reshape(-1, 3).max(axis=0) - T.reshape(-1, 3).min(axis=0)
    zs = T.reshape(-1, 3)[:, 2]
    name = "打印_%d_%s.3mf" % (i, cn)
    write_3mf(os.path.join(OUT, name), [(mesh_xml(T), cn)], cn, note)
    fits = dim[0] <= BED and dim[1] <= BED
    print("  %-26s %6.1f x %6.1f x %6.1f | 落床 z=%.1f | %s %s" % (
        name, dim[0], dim[1], dim[2], zs.min(), note, "✅" if fits else "✗超床"))

print()
print("=== 拼盘(减少换料次数)===")
for pname, fns in PLATES:
    objs = []
    if len(fns) == 2:
        a, b = fns
        da = cache[a].reshape(-1, 3); db = cache[b].reshape(-1, 3)
        wa = da.max(axis=0)[0] - da.min(axis=0)[0]; wb = db.max(axis=0)[0] - db.min(axis=0)[0]
        ha = da.max(axis=0)[1] - da.min(axis=0)[1]; hb = db.max(axis=0)[1] - db.min(axis=0)[1]
        if wa + wb + GAP <= BED:          # 并排
            Ta = place(cache[a], BED / 2 - (wa + wb + GAP) / 2 + wa / 2, BED / 2)
            Tb = place(cache[b], BED / 2 + (wa + wb + GAP) / 2 - wb / 2, BED / 2)
        else:                             # 上下叠放
            Ta = place(cache[a], BED / 2, BED / 2 - (ha + hb + GAP) / 2 + ha / 2)
            Tb = place(cache[b], BED / 2, BED / 2 + (ha + hb + GAP) / 2 - hb / 2)
        objs = [(mesh_xml(Ta), a), (mesh_xml(Tb), b)]
        mx = max(wa, wb, (wa + wb + GAP) if wa + wb + GAP <= BED else wa)
        my = max(ha, hb, (ha + hb + GAP) if wa + wb + GAP > BED else ha)
        ok = mx <= BED and my <= BED
        name = "打印_%s.3mf" % pname
        write_3mf(os.path.join(OUT, name), objs, pname, "同盘摆放,减少装夹次数")
        print("  %-30s 占用 %.0f x %.0f | %s" % (name, mx, my, "✅" if ok else "✗超床"))

# 参数卡
with open(os.path.join(OUT, "打印参数_分件速查.txt"), "w", encoding="utf-8") as f:
    f.write("""分件打印速查(v32)—— 每个 3mf 已摆好姿态、落床、居中于 256x256 床
=====================================================================
文件名                     姿态                     支撑      裙边
打印_1_外壳.3mf            立着打(底面朝下)        需要      需要 5mm
打印_2_正面面板.3mf        外面朝下平躺             免        不需要
打印_3_背板.3mf            外面朝下平躺(支架朝上)   免        不需要
打印_4_底面盖板.3mf        外面朝下平躺             免        不需要
打印_5_走线槽盖.3mf        盖面朝下平躺             免        不需要
打印_拼盘A_正面面板+走线槽盖.3mf                      免        不需要
打印_拼盘B_背板+底面盖板.3mf                          免        不需要
打印_拼盘C_下半部外壳+上半部外壳.3mf(256床极限:占 238x255)  需      各需 5mm 裙边
打印_7_下壳A段.3mf        原姿态(z0~75)            需        需 5mm
打印_8_下壳B段.3mf        翻转180°(分型面贴床)      少量      不需要
打印_拼盘D_下壳A段+下壳B段.3mf                                                                  

切片参数(全部件通用)
  层高 0.2mm | 首层 0.2 | 墙 3 层(外壳可 2 层省料) | 顶/底实心 4/4
  填充 15% | 喷嘴 220°C | 热床 55°C | 外墙 150mm/s | 首层 30mm/s | 冷却 100%
  支撑:仅外壳需要(树状支撑更易掏出);墙顺序建议"外墙优先"(保卡扣精度)
  外壳建议加 5mm 裙边(底面开口大,贴床面积仅 98.7cm²)

耗材估算:外壳 512g / 正面 113g / 背板 121g / 底面 89g / 槽盖 22g = 合计约 857g
""")
print()
print("已生成 11 个 3mf + 打印参数_分件速查.txt")
