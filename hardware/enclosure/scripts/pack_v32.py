# v32 出包:5 件 3MF + 耗材 + 桌面同步
import os, sys, struct, zipfile, shutil
import numpy as np
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
SRC = r"C:\Users\SomaRia\Documents\deepseek-harness\default-workspace\host-shell"
PKG = r"C:\Users\SomaRia\Desktop\pj\小爱风格主机外壳_打印"
PRJ = r"C:\Users\SomaRia\Desktop\pj\小爱风格主机外壳_Blender"
NAMES = ["Host_Shell", "Front_Cover", "Back_Cover", "Bottom_Cover", "Channel_Cover"]
TITLES = ["Shell 外壳(v32 背面可拆+底面可拆)", "Front 正面面板 184x138",
          "Back 背板 184x138(含树莓派支架)", "Bottom 底面盖板 205x92 R8.5",
          "Channel 走线槽盖(R25 裁形)"]
WALL, INFILL, DENSITY = 1.2, 0.15, 1.24


def read_stl(p):
    with open(p, "rb") as f:
        f.read(80)
        n = struct.unpack("<I", f.read(4))[0]
        raw = f.read(n * 50)
    v = np.frombuffer(raw, dtype=np.dtype([("n", "<3f4"), ("v", "<9f4"), ("a", "<u2")]))
    return v["v"].reshape(n, 3, 3).astype(np.float64)


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


ct = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
      ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
      ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n</Types>\n')
rl = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
      ' <Relationship Target="/3D/3dmodel.model" Id="rel-1" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>\n')
tot = 0.0
print("%-16s %10s %11s %9s" % ("零件", "实体cm3", "面积cm2", "耗材g"))
for fn, tt in zip(NAMES, TITLES):
    T = read_stl(os.path.join(SRC, fn + ".stl"))
    n = np.cross(T[:, 1] - T[:, 0], T[:, 2] - T[:, 0])
    area = np.linalg.norm(n, axis=1).sum() / 2.0
    vol = abs(float(np.einsum("ij,ij->i", T[:, 0], np.cross(T[:, 1], T[:, 2])).sum() / 6.0))
    g = (area * WALL + max(0.0, vol - area * WALL) * INFILL) / 1000.0 * DENSITY
    tot += g
    print("%-16s %10.1f %11.1f %9.1f" % (fn, vol / 1000, area / 100, g))
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" '
             'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">\n'
             ' <metadata name="Application">DeepSeek-Harness</metadata>\n <metadata name="Title">%s</metadata>\n'
             ' <resources>\n  <object id="1" type="model"><mesh>\n%s\n  </mesh></object>\n </resources>\n'
             ' <build>\n  <item objectid="1"/>\n </build>\n</model>\n' % (tt, mesh_xml(T)))
    with zipfile.ZipFile(os.path.join(PKG, fn + ".3mf"), "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ct)
        z.writestr("_rels/.rels", rl)
        z.writestr("3D/3dmodel.model", model)
print("合计 ≈ %.0f g" % tot)
for fn in NAMES:
    for ext in (".stl", ".step"):
        shutil.copy2(os.path.join(SRC, fn + ext), os.path.join(PKG, fn + ext))
        shutil.copy2(os.path.join(SRC, fn + ext), os.path.join(PRJ, fn + ext))
for d, pre in ((PKG, "7_"), (PRJ, "6_")):
    for f in os.listdir(d):
        if f.endswith(".FCStd"):
            os.remove(os.path.join(d, f))
    shutil.copy2(os.path.join(SRC, "Host_Shell.FCStd"), os.path.join(d, pre + "主机外壳(v32).FCStd"))
zp = os.path.join(PKG, "小爱风格主机外壳_打印包.zip")
if os.path.exists(zp):
    os.remove(zp)
with zipfile.ZipFile(zp, "w", zipfile.ZIP_DEFLATED) as z:
    for fn in NAMES:
        z.write(os.path.join(PKG, fn + ".3mf"), fn + ".3mf")
    z.write(os.path.join(PKG, "打印参数.txt"), "打印参数.txt")
print("桌面同步完成(五件)")
