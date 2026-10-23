# 主机外壳 v26:①背面改为卡扣可打开面板(树莓派支架移到背板上)②走线槽盖卡扣改为带导入斜面的弹性卡扣
import math, os, sys
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass
import FreeCAD as App
import Part
from FreeCAD import Vector

D = r"C:\Users\SomaRia\Documents\deepseek-harness\default-workspace\host-shell"
os.makedirs(D, exist_ok=True)

T = 6.0
HB = 150.0
SCR_W, SCR_H, SCR_T = 167.0, 102.0, 13.0
ANG = 55.0
SIN, COS = math.sin(math.radians(ANG)), math.cos(math.radians(ANG))
CLR = 0.5
BORD_X, BORD_U = 5.0, 8.5
PLATE_T = SCR_T + CLR
POCK_W = SCR_W + 2 * CLR + 3.0
POCK_H = SCR_H + 2 * CLR
PLATE_W = POCK_W + 45.0 + 2 * BORD_X
PLATE_L = POCK_H + 2 * BORD_U
SLANT_V, SLANT_D = PLATE_L * SIN, PLATE_L * COS
HT = HB + SLANT_V
W = T + PLATE_W + T
DEP = 125.0
POCK_X0 = T + BORD_X
CH_X0 = POCK_X0 + POCK_W
# 正面/背面开口:开口 = 面板 + 单边 0.5 间隙(面板嵌入后与外壳齐平)
PANEL_W, PANEL_H = 184.0, 138.0   # 收窄:必须落在 R25 平面区(25~213)内
PX0, PZ0 = (W - PANEL_W) / 2.0, (HB - PANEL_H) / 2.0
OX0, OX1 = PX0 - 0.5, PX0 + PANEL_W + 0.5
OZ0, OZ1 = PZ0 - 0.5, PZ0 + PANEL_H + 0.5
PANEL_T, RIM_T, RIM_H, BARB = 6.0, 3.0, 8.0, 1.0   # v32: 卡边 8mm,让开底面挡边
COVER_T, COVER_R = PLATE_T, 4.0
COVER_FLOOR = 1.5           # 槽盖顶板厚度(按用户要求 1.5,给走线留更多空间)
CLIP_T = 1.2                # 弹性卡扣厚度(独立于面板;原等于面板厚导致弹力 82N 压不进)
# 走线槽盖卡扣(新:带 45 度导入斜面的弹性卡扣)
CLIP_N, CLIP_W, CLIP_H = 5, 1.4, 0.6      # 5 个(两端各 17.8mm),凸出 0.6mm
CLIP_D0, CLIP_D1 = 2.0, 5.0                # 卡扣在厚度方向的区间
# 树莓派 3B
RPI_HX, RPI_HY, RPI_Wd = 58.0, 49.0, 56.0
RPI_STAND, BOSS_D, PILOT_D, RPI_Z0 = 6.0, 9.0, 2.2, 40.0

print("=== 主机外壳 v26 ===")
print("机体 %.0f x %.0f x %.0f + 斜面 %.0f(55 度)-> 总高 %.1f | 壁厚 %.0f" % (W, DEP, HB, PLATE_L, HT, T))
print("正面开口 %.0f x %.0f + 面板(4 卡爪 + 撬槽)" % (OX1 - OX0, OZ1 - OZ0))
print("v31: 背面封死(一体化) + 底面可拆(206x93 R9) + 树莓派支架长在壳体内壁")
print("走线槽盖: %d 个弹性卡扣,凸出 %.1fmm + 45 度导入斜面,配 %.1fmm 深卡槽" % (CLIP_N, CLIP_W, CLIP_H))

doc = App.newDocument("Host_Shell")
doc.Label = "Host_Shell"


def sbox(w, h, d, x0, u0, back=0.0):
    b = Part.makeBox(w, h, d)
    b.rotate(Vector(0, 0, 0), Vector(1, 0, 0), ANG)
    b.translate(Vector(x0, 0, HB) + Vector(0, COS, SIN) * u0 + Vector(0, -SIN, COS) * (-back))
    return b


def sprism(pts, u0, u1, x0):
    """(x, d) 平面多边形沿斜面 u 拉伸"""
    poly = [Vector(x0 + px, 0.0, pd) for px, pd in pts]
    poly.append(poly[0])
    f = Part.Face(Part.Wire(Part.makePolygon(poly)))
    s = f.extrude(Vector(0, u1 - u0, 0))
    s.rotate(Vector(0, 0, 0), Vector(1, 0, 0), ANG)
    s.translate(Vector(0, 0, HB) + Vector(0, COS, SIN) * u0)
    return s


def do_fillet(shape, radius, edges, tag):
    if not edges:
        print("  [跳过] %s" % tag); return shape
    try:
        out = shape.makeFillet(radius, edges)
        print("  [OK] %s R%.1f (%d 边)" % (tag, radius, len(edges)))
        return out
    except Exception as ex:
        print("  [失败] %s R%.1f: %s" % (tag, radius, ex)); return shape


# ---------------- 外壳 ----------------
prof = [Vector(0, 0, 0), Vector(0, 0, HB), Vector(0, SLANT_D, HT),
        Vector(0, DEP, HT), Vector(0, DEP, 0), Vector(0, 0, 0)]
outer = Part.Face(Part.Wire(Part.makePolygon(prof))).extrude(Vector(W, 0, 0))
vert = [e for e in outer.Edges
        if abs(e.Vertexes[0].Point.x - e.Vertexes[-1].Point.x) < 1e-6
        and abs(e.Vertexes[0].Point.y - e.Vertexes[-1].Point.y) < 1e-6
        and abs(e.Vertexes[0].Point.z - e.Vertexes[-1].Point.z) > 10.0]
print("=== 圆角 ===")
outer = do_fillet(outer, 25.0, vert, "4 条竖角")
outer = do_fillet(outer, 8.0, [e for e in outer.Edges
                               if abs(e.BoundBox.ZMin - HT) < 1e-6 and abs(e.BoundBox.ZMax - HT) < 1e-6], "顶面棱")
outer = do_fillet(outer, 8.0, [e for e in outer.Edges
                               if abs(e.BoundBox.XLength) < 1e-6 and e.BoundBox.ZMin > HB - 1
                               and e.BoundBox.ZMax < HT + 1 and abs(e.BoundBox.YLength) > 20.0], "斜面两侧棱")
outer = outer.removeSplitter()


def inset(pts, d):
    py, pz = d * SIN, HB - d * COS
    t = (d - py) / SLANT_D
    t2 = (HT - d - pz) / SLANT_V
    return [Vector(0, d, d), Vector(0, d, pz + t * SLANT_V), Vector(0, py + t2 * SLANT_D, HT - d),
            Vector(0, DEP - d, HT - d), Vector(0, DEP - d, d), Vector(0, d, d)]


_inner = Part.Face(Part.Wire(Part.makePolygon(inset(prof, T)))).extrude(Vector(W - 2 * T, 0, 0)) \
    .translate(Vector(T, 0, 0))
# 内腔四角必须与外圆角同轴圆角(R19 = 25 - 6),否则直角尖点会戳穿外圆角,
# 在四个竖棱处形成贯穿全高的开口缝(能直接看到内部)
_vedges = []
for _e in _inner.Edges:
    _p0, _p1 = _e.Vertexes[0].Point, _e.Vertexes[1].Point
    if abs(_p0.x - _p1.x) < 1e-6 and abs(_p0.y - _p1.y) < 1e-6 and abs(_p0.z - _p1.z) > 50.0:
        if (abs(_p0.x - T) < 1e-6 or abs(_p0.x - (W - T)) < 1e-6) and \
           (abs(_p0.y - T) < 1e-6 or abs(_p0.y - (DEP - T)) < 1e-6):
            _vedges.append(_e)
print("内腔竖棱待圆角: %d 条" % len(_vedges))
_inner = _inner.makeFillet(19.0, _vedges)   # 25 - 6
body = outer.cut(_inner)
print("挖内腔: %.1f cm3 solids=%d valid=%s" % (body.Volume / 1000, len(body.Solids), body.isValid()))

# ---------------- 屏幕板 + 屏幕槽 ----------------
body = body.fuse(sbox(PLATE_W, PLATE_L, PLATE_T, T, 0.0))
body = body.cut(sbox(POCK_W, POCK_H, PLATE_T, POCK_X0, BORD_U, back=0.0))
for _uu in (BORD_U, BORD_U + POCK_H - 12.0):
    body = body.fuse(sbox(3.0, 12.0, 3.0, POCK_X0 + POCK_W - 3.0, _uu, back=0.0))
body = body.cut(Part.makeBox(W + 20, DEP + 40, 40, Vector(-10, -10, HT)))
body = body.cut(Part.makeBox(W + 20, 50, HT + 40, Vector(-10, -50, -10)))

# ---------------- 走线区贯通 + 卡槽 ----------------
CH_U0 = 12.0   # v32: 起点沿斜面上移 12mm,避开 R25 竖向圆角区(消除圆角处长条缝)
body = body.cut(sbox(46.0, PLATE_L - 1.0 - CH_U0, (T + 4.0) + PLATE_T + 0.5,
                     POCK_X0 + POCK_W - 0.5, CH_U0, back=T + 4.0))
# 走线槽盖的卡槽(3 个)
for i in range(CLIP_N):
    uy = CH_U0 + (PLATE_L - 1.0 - CH_U0) * (i + 1) / (CLIP_N + 1)
    body = body.cut(sbox(CLIP_W + 0.6, 12.0, 3.2, CH_X0 + 45.0 - 0.6,
                         uy - 6.0, back=-(CLIP_D0 - 0.2)))

# 卡槽顶面 45 度楔形释放面(斜面坐标系:局部 x=周向,局部 d=壁厚方向)
# 卡扣退出(+d)时,肩部沿斜面被推回 -x,从而内缩脱开 → 免工具可拆槽盖
_CH_W = CLIP_W + 0.6
_CH_X0 = CH_X0 + 45.0 - 0.6
_CH_D0 = CLIP_D0 - 0.2 + 3.2          # 卡槽顶面所在 d
for i in range(CLIP_N):
    uy = CH_U0 + (PLATE_L - 1.0 - CH_U0) * (i + 1) / (CLIP_N + 1)
    # 三角形:((x0,d0) -> (x0+w, d0+w) -> (x0+w, d0)) 的上方材料被切掉
    _tri = [(_CH_X0, _CH_D0), (_CH_X0 + _CH_W, _CH_D0 + _CH_W), (_CH_X0 + _CH_W, _CH_D0)]
    _pts = []
    for _px, _pd in _tri:
        _pts.append(Vector(_px, uy * COS - _pd * SIN, HB + uy * SIN + _pd * COS))
    _pts.append(_pts[0])
    _f = Part.Face(Part.Wire(Part.makePolygon(_pts)))
    _dv = Vector(0.0, -SIN, COS) * (PLATE_L * 0 + 0)  # 占位,下面用 u 方向挤出
    _p0 = Vector(_CH_X0, (uy - 6.2) * COS - _CH_D0 * SIN, HB + (uy - 6.2) * SIN + _CH_D0 * COS)
    _p1 = Vector(_CH_X0, (uy + 6.2) * COS - _CH_D0 * SIN, HB + (uy + 6.2) * SIN + _CH_D0 * COS)
    _ext = _p1 - _p0
    body = body.cut(_f.extrude(_ext))
print("走线槽盖卡槽 45 度楔形释放面 x%d" % CLIP_N)
print("屏幕板+走线区+卡槽: %.1f cm3 valid=%s" % (body.Volume / 1000, body.isValid()))

# ---------------- 正/背面开口(开口=面板+0.5 间隙,内侧 3mm 台阶供卡边落座)----------------
for _yc in (-2.0, DEP - T - 2.0):   # v32: 背面恢复可拆卸
    body = body.cut(Part.makeBox(OX1 - OX0, T + 4.0, OZ1 - OZ0, Vector(OX0, _yc, OZ0)))
    _step_y = 4.0 if _yc < 0 else (DEP - T - 1.0)
    _dir = -1.0 if _yc < 0 else 1.0
    body = body.cut(Part.makeBox(OX1 - OX0 + 6.0, 3.0, OZ1 - OZ0 + 6.0,
                                 Vector(OX0 - 3.0, _step_y, OZ0 - 3.0)))
# ---------------- 右侧壁开孔:距底面 15mm 居中,10 x 4 ----------------
R_HOLE_W, R_HOLE_H, R_HOLE_Z = 10.0, 4.0, 15.0
body = body.cut(Part.makeBox(T + 4.0, R_HOLE_W, R_HOLE_H,
                             Vector(W - T - 2.0, DEP / 2.0 - R_HOLE_W / 2.0,
                                    R_HOLE_Z - R_HOLE_H / 2.0)))
print("右侧壁孔: 10 x 4,中心 (x=%.0f, y=%.1f, z=%.1f)" % (W, DEP / 2.0, R_HOLE_Z))

# ---------------- 与 10x4 孔同面:其中心正上方 15mm 处的 14 x 9 孔 ----------------
R2_W, R2_H = 12.5, 8.5   # 1.25 x 0.85 cm
R2_Z = R_HOLE_Z + 15.0                       # 15 + 15 = 30
body = body.cut(Part.makeBox(T + 4.0, R2_W, R2_H,
                             Vector(W - T - 2.0, DEP / 2.0 - R2_W / 2.0, R2_Z - R2_H / 2.0)))
print("新增孔: %.0f x %.0f,中心 (x=%.0f, y=%.1f, z=%.1f)(10x4 孔正上方 15mm)" % (
    R2_W, R2_H, W, DEP / 2.0, R2_Z))

# ---------------- 右侧壁 RJ45 网口:树莓派同高、居中,标准 11.68x8.0 + 1mm 冗余 ----------------
RJ45_W, RJ45_H = 11.68 + 1.0, 8.0 + 1.0          # 12.68 x 9.0
RJ45_Z = RPI_Z0 + RPI_Wd / 2.0                    # 与树莓派 3B 板中心同高 = 68
body = body.cut(Part.makeBox(T + 4.0, RJ45_W, RJ45_H,
                             Vector(W - T - 2.0, DEP / 2.0 - RJ45_W / 2.0,
                                    RJ45_Z - RJ45_H / 2.0)))
print("RJ45 网口: %.2f x %.2f,中心 (x=%.0f, y=%.1f, z=%.1f)" % (RJ45_W, RJ45_H, W, DEP / 2.0, RJ45_Z))
# 网口下方一对 M2 固定耳(可选,便于将来加装卡扣或防尘挡片)
for _dz in (-1, 1):
    body = body.cut(Part.makeCylinder(1.1, T + 4.0, Vector(W - T - 2.0, DEP / 2.0 + _dz * (RJ45_W / 2.0 + 4.0), RJ45_Z), Vector(1, 0, 0)))
print("网口两侧 M2.2 预留孔: y=%.1f / %.1f, z=%.1f" % (DEP / 2.0 - RJ45_W / 2.0 - 4.0, DEP / 2.0 + RJ45_W / 2.0 + 4.0, RJ45_Z))

# 手指凹位(正/背各一个,配合撬槽)
body = body.cut(Part.makeBox(30.0, 4.0, 5.0, Vector(W / 2 - 15, -1.0, PZ0 - 7.0)))
body = body.cut(Part.makeBox(30.0, 4.0, 5.0, Vector(W / 2 - 15, DEP - 3.0, PZ0 - 7.0)))
body = body.removeSplitter()
# 开口四角补 R6.5 圆角(正面/背面),使与面板 R6 形成均匀 0.5 间隙
_R = 6.5
_corners = [(-1, -1), (1, -1), (-1, 1), (1, 1)]
for _yc, _tag in ((0.0, "front"), (DEP - T, "back")):
    for _sx, _sz in _corners:
        _cx = (PX0 - 0.5 + _R) if _sx < 0 else (PX0 + PANEL_W + 0.5 - _R)
        _cz = (PZ0 - 0.5 + _R) if _sz < 0 else (PZ0 + PANEL_H + 0.5 - _R)
        _bx0 = OX0 - 1.0 if _sx < 0 else _cx
        _bx1 = _cx if _sx < 0 else OX1 + 1.0
        _bz0 = OZ0 - 1.0 if _sz < 0 else _cz
        _bz1 = _cz if _sz < 0 else OZ1 + 1.0
        _blk = Part.makeBox(_bx1 - _bx0, T, _bz1 - _bz0, Vector(_bx0, _yc, _bz0))
        _cyl = Part.makeCylinder(_R, T + 2.0, Vector(_cx, _yc - 1.0, _cz), Vector(0, 1, 0))
        body = body.fuse(_blk.cut(_cyl))
    print("  %s 开口四角补 R%.1f" % (_tag, _R))

# ================= v31:背面封死 + 树莓派支架移入内壁 + 底面可拆开口 =================
# --- 底面开口:206 x 93 R9,内侧留 10mm 台阶给卡爪勾住 ---
BOT_OX0, BOT_OY0 = T + 10.0, T + 10.0                    # 16, 16
BOT_OX1, BOT_OY1 = W - T - 10.0, DEP - T - 10.0          # 222, 109
BOT_R = 9.0
BOT_STEP_W, BOT_STEP_D = 3.0, 6.0                        # 台阶宽(向外扩 6)/深 3
BOT_T = T
# 主开口(矩形贯通),四角随后切成 R9
body = body.cut(Part.makeBox(BOT_OX1 - BOT_OX0, BOT_OY1 - BOT_OY0, BOT_T + 4.0,
                             Vector(BOT_OX0, BOT_OY0, -2.0)))
for _sx, _sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
    _cx = (BOT_OX0 + BOT_R) if _sx < 0 else (BOT_OX1 - BOT_R)
    _cy = (BOT_OY0 + BOT_R) if _sy < 0 else (BOT_OY1 - BOT_R)
    _bx0, _bx1 = (BOT_OX0, _cx) if _sx < 0 else (_cx, BOT_OX1)
    _by0, _by1 = (BOT_OY0, _cy) if _sy < 0 else (_cy, BOT_OY1)
    _blk = Part.makeBox(_bx1 - _bx0, _by1 - _by0, BOT_T + 4.0, Vector(_bx0, _by0, -2.0))
    _cyl = Part.makeCylinder(BOT_R, BOT_T + 6.0, Vector(_cx, _cy, -3.0), Vector(0, 0, 1))
    body = body.cut(_blk.cut(_cyl))
# v31: 不设台阶 —— 卡爪直接勾住底壁内表面(z=6),受力面即内表面
print("底面开口 %.0f x %.0f R%.0f(贯通底壁,卡爪勾内表面)" % (
    BOT_OX1 - BOT_OX0, BOT_OY1 - BOT_OY0, BOT_R))
body = body.removeSplitter()

# ================= 遮光挡边:开口内侧一圈 2mm x 3mm =================
# 作用:面板与开口的 0.5mm 缝隙虽小,但直通腔内(能看穿到对面)。挡边藏在壳内、贴着开口边,
#       与面板卡边只是面接触(0 干涉),却把透光路径完全堵死。
_LIP_W, _LIP_H = 2.0, 3.0
for _yc, _dir in ((0.0, 1.0), (DEP - T, -1.0)):
    _y0 = _yc + T * _dir if _dir > 0 else _yc - _LIP_H
    body = body.fuse(Part.makeBox(OX1 - OX0 + 2 * _LIP_W, _LIP_H, _LIP_W,
                                  Vector(OX0 - _LIP_W, _y0, OZ0 - _LIP_W)))
    body = body.fuse(Part.makeBox(OX1 - OX0 + 2 * _LIP_W, _LIP_H, _LIP_W,
                                  Vector(OX0 - _LIP_W, _y0, OZ1)))
    body = body.fuse(Part.makeBox(_LIP_W, _LIP_H, OZ1 - OZ0,
                                  Vector(OX0 - _LIP_W, _y0, OZ0)))
    body = body.fuse(Part.makeBox(_LIP_W, _LIP_H, OZ1 - OZ0,
                                  Vector(OX1, _y0, OZ0)))
# 走线槽盖下端"插舌槽"(第 6 处固定:固定式卡舌,不增加按压力)
body = body.cut(sbox(46.0, 3.6, 2.7, CH_X0 - 0.5, CH_U0 - 3.3, back=0.0))
print("  走线槽下端插舌槽: 46 x 3.6 x 2.7(槽盖下端插舌由此固定)")

print("  正面/背面开口内侧遮光挡边: %.0fmm x %.0fmm" % (_LIP_W, _LIP_H))
# 底面开口内侧挡边
body = body.fuse(Part.makeBox(BOT_OX1 - BOT_OX0 + 2 * _LIP_W, _LIP_W, _LIP_H,
                              Vector(BOT_OX0 - _LIP_W, BOT_OY0 - _LIP_W, BOT_T)))
body = body.fuse(Part.makeBox(BOT_OX1 - BOT_OX0 + 2 * _LIP_W, _LIP_W, _LIP_H,
                              Vector(BOT_OX0 - _LIP_W, BOT_OY1, BOT_T)))
body = body.fuse(Part.makeBox(_LIP_W, BOT_OY1 - BOT_OY0, _LIP_H,
                              Vector(BOT_OX0 - _LIP_W, BOT_OY0, BOT_T)))
body = body.fuse(Part.makeBox(_LIP_W, BOT_OY1 - BOT_OY0, _LIP_H,
                              Vector(BOT_OX1, BOT_OY0, BOT_T)))
print("  底面开口内侧遮光挡边: %.0fmm x %.0fmm" % (_LIP_W, _LIP_H))

print("外壳整体: %.1f cm3 solids=%d valid=%s" % (body.Volume / 1000, len(body.Solids), body.isValid()))


# ---------------- 外壳侧:正面面板卡爪的 45 度释放斜面 ----------------
# 前面板已打印、不可改 → 只改外壳:在卡肩顶住的面(y = PANEL_T)上切 45 度斜面。
# 往外拉面板时,卡爪沿斜面自动内缩,免工具即可从外面打开。
# 卡爪位置:左 x 26~26.5 / 右 211.5~212;z = PZ0+30 与 PZ0+PANEL_H-30,各宽 3mm
_REL = 0.7                      # 斜面在两个方向的投影(45 度)
for _sz in (PZ0 + 30.0, PZ0 + PANEL_H - 30.0):
    for _sx in (-1, 1):
        if _sx < 0:
            _pts = [(OX0 - _REL, PANEL_T), (OX0, PANEL_T), (OX0, PANEL_T + _REL)]
        else:
            _pts = [(OX1 + _REL, PANEL_T), (OX1, PANEL_T), (OX1, PANEL_T + _REL)]
        _poly = [Vector(px, py, _sz - 2.0) for px, py in _pts]
        _poly.append(_poly[0])
        body = body.cut(Part.Face(Part.Wire(Part.makePolygon(_poly))).extrude(Vector(0, 0, 7.0)))
print("外壳侧释放斜面:正面面板 4 个卡爪处各切 45 度(%.1f x %.1f),面板无需改动" % (_REL, _REL))

# ---------------- 外壳侧:底面盖板 4 卡爪的释放让位 ----------------
# 底面盖板已打印 → 只改外壳:把卡爪行程内的台阶让开(x/y 向外各让 1.2mm,z 6~9.6),
# 卡爪即可沿直壁滑出;盖板仍由卡肩压在底壁内表面(z=6)上,保持力不变。
_BRP, _BRZ = 1.2, 3.6
for _bx in (BOT_OX0 - _BRP, BOT_OX1):
    body = body.cut(Part.makeBox(_BRP, BOT_OY1 - BOT_OY0 + 2 * _BRP,
                                 _BRZ, Vector(_bx, BOT_OY0 - _BRP, BOT_T)))
# 底壁内表面(z=BOT_T)在卡爪行程处倒 45 度角 → 卡爪退出时沿斜面内缩
_CH = 1.0
for _bx, _sx in ((BOT_OX0, 1.0), (BOT_OX1, -1.0)):
    _p3 = [(BOT_OX0 - _CH, BOT_T, 0.0) if _sx > 0 else (BOT_OX1, BOT_T, 0.0),
           (BOT_OX0, BOT_T, 0.0) if _sx > 0 else (BOT_OX1 + _CH, BOT_T, 0.0)]
    _tri = ([(BOT_OX0 - _CH, BOT_T), (BOT_OX0, BOT_T), (BOT_OX0, BOT_T + _CH)] if _sx > 0
            else [(BOT_OX1, BOT_T), (BOT_OX1 + _CH, BOT_T), (BOT_OX1, BOT_T + _CH)])
    for _by in (BOT_OY0 - 0.5, BOT_OY1 - 3.0):
        _poly = [Vector(px, _by, pz) for px, pz in _tri]
        _poly.append(_poly[0])
        body = body.cut(Part.Face(Part.Wire(Part.makePolygon(_poly))).extrude(Vector(0, 3.5, 0)))
for _by, _sy in ((BOT_OY0, 1.0), (BOT_OY1, -1.0)):
    _tri2 = ([(BOT_OY0 - _CH, BOT_T), (BOT_OY0, BOT_T), (BOT_OY0, BOT_T + _CH)] if _sy > 0
             else [(BOT_OY1, BOT_T), (BOT_OY1 + _CH, BOT_T), (BOT_OY1, BOT_T + _CH)])
    for _bx in (BOT_OX0 - 0.5, BOT_OX1 - 3.0):
        _poly2 = [Vector(_bx, py, pz) for py, pz in _tri2]
        _poly2.append(_poly2[0])
        body = body.cut(Part.Face(Part.Wire(Part.makePolygon(_poly2))).extrude(Vector(3.5, 0, 0)))
print("底壁内表面已倒 45 度释放角(4 处)")
for _by in (BOT_OY0 - _BRP, BOT_OY1):
    body = body.cut(Part.makeBox(BOT_OX1 - BOT_OX0 + 2 * _BRP, _BRP,
                                 _BRZ, Vector(BOT_OX0 - _BRP, _by, BOT_T)))
print("外壳侧:底面盖板卡爪行程内台阶已让开(%.1f x %.1f)" % (_BRP, _BRZ))

# ---------------- 面板(正面 / 背面通用;嵌入开口,与外壳齐平)----------------
def make_panel(front, add_rpi, tag):
    y_plate = 0.0 if front else (DEP - PANEL_T)
    y_rim = PANEL_T if front else (DEP - PANEL_T - RIM_H)
    p = Part.makeBox(PANEL_W, PANEL_T, PANEL_H, Vector(PX0, y_plate, PZ0))
    R = 6.0
    for (cx, cz, dx, dz) in ((PX0, PZ0, R, R), (PX0 + PANEL_W - R, PZ0, 0.0, R),
                             (PX0, PZ0 + PANEL_H - R, R, 0.0),
                             (PX0 + PANEL_W - R, PZ0 + PANEL_H - R, 0.0, 0.0)):
        cb = Part.makeBox(R, PANEL_T + 2.0, R, Vector(cx, y_plate - 1.0, cz))
        cc = Part.makeCylinder(R, PANEL_T + 2.0, Vector(cx + dx, y_plate - 1.0, cz + dz), Vector(0, 1, 0))
        p = p.cut(cb.cut(cc))
    # 3mm 宽环形卡边(向机身内侧 9mm)
    ring = Part.makeBox(PANEL_W, RIM_H, PANEL_H, Vector(PX0, y_rim, PZ0))
    ring = ring.cut(Part.makeBox(PANEL_W - 2 * RIM_T, RIM_H + 2, PANEL_H - 2 * RIM_T,
                                 Vector(PX0 + RIM_T, y_rim - 1.0, PZ0 + RIM_T)))
    # 卡边四角与开口圆角一致(R6.5),贴住开口边缘 -> 堵住角部透光缝且不重叠
    _RC = 6.5
    for (_cx, _cz, _dx, _dz) in ((PX0 - 0.5, PZ0 - 0.5, _RC, _RC),
                                 (PX0 + PANEL_W + 0.5 - _RC, PZ0 - 0.5, 0.0, _RC),
                                 (PX0 - 0.5, PZ0 + PANEL_H + 0.5 - _RC, _RC, 0.0),
                                 (PX0 + PANEL_W + 0.5 - _RC, PZ0 + PANEL_H + 0.5 - _RC, 0.0, 0.0)):
        _cb = Part.makeBox(_RC, RIM_H + 2, _RC, Vector(_cx, y_rim - 1.0, _cz))
        _cc = Part.makeCylinder(_RC, RIM_H + 2, Vector(_cx + _dx, y_rim - 1.0, _cz + _dz), Vector(0, 1, 0))
        ring = ring.cut(_cb.cut(_cc))
    p = p.fuse(ring)
    # 4 个卡爪(凸出 1.2mm,带 45 度导入斜面,斜面朝装入方向)
    for sx in (-1, 1):
        for sz in (PZ0 + 30.0, PZ0 + PANEL_H - 30.0):
            x0 = (PX0 - BARB) if sx < 0 else (PX0 + PANEL_W)
            # 底部 45 度导入斜面(y 3->4) + 平直卡肩(y 4->6, 顶住外壳台阶)
            prof = [(0.0, 3.0), (BARB, 4.0), (BARB, PANEL_T), (0.0, PANEL_T)] if front else \
                   [(0.0, PANEL_T), (BARB, PANEL_T), (BARB, 2.0), (0.0, 3.0)]
            poly = [Vector(x0 + px, y_plate + py, sz) for px, py in prof]
            poly.append(poly[0])
            p = p.fuse(Part.Face(Part.Wire(Part.makePolygon(poly))).extrude(Vector(0, 0, 3.0)))
            # 卡爪两侧掏薄:卡边壁 3mm -> 卡爪处留 1.5mm 弹性舌片
            if sx < 0:
                _tx = PX0 + 1.5
            else:
                _tx = PX0 + PANEL_W - 3.5
            p = p.cut(Part.makeBox(2.5, RIM_H + 2.0, 16.0, Vector(_tx, y_rim - 1.0, sz - 8.0)))
    # 撬槽(双台阶:外圈导入斜口 34x1.5x6 + 内层受力槽 30x3x4)
    if front:
        p = p.cut(Part.makeBox(34.0, 1.5, 6.0, Vector(W / 2 - 17, -0.5, PZ0 - 1.5)))
        p = p.cut(Part.makeBox(30.0, 4.0, 4.0, Vector(W / 2 - 15, -1.0, PZ0 - 0.5)))
    else:
        # 背面:双台阶(外圈导入斜口 + 内层受力槽),与正面一致
        p = p.cut(Part.makeBox(34.0, 1.5, 6.0, Vector(W / 2 - 17, DEP - 1.0, PZ0 - 1.5)))
        p = p.cut(Part.makeBox(30.0, 4.0, 4.0, Vector(W / 2 - 15, DEP - 3.0, PZ0 - 0.5)))
    if add_rpi:
        back_in = DEP - T
        rpi_cx = T + (W - 2 * T) / 2.0
        z_cx = RPI_Z0 + RPI_Wd / 2.0
        for sx in (-1, 1):
            for sz in (-1, 1):
                hx, hz = rpi_cx + sx * RPI_HX / 2.0, z_cx + sz * RPI_HY / 2.0
                p = p.fuse(Part.makeCylinder(BOSS_D / 2, RPI_STAND + 1.5,
                                             Vector(hx, back_in - RPI_STAND, hz), Vector(0, 1, 0)))
                p = p.cut(Part.makeCylinder(PILOT_D / 2, RPI_STAND + 1,
                                            Vector(hx, back_in - RPI_STAND - 0.5, hz), Vector(0, 1, 0)))
        # 树莓派两侧的磁吸充电接口孔(数据与 Cone_Case 完全一致:S=1.2)
        #   露出开口 26.4 x 4.8(= 22.0*S x 4.0*S);模块腔 27.6 x 4.4(适配 27x4 模块 + 0.3/0.2 间隙);
        #   唇口 2.0mm。装法与 CC 相同:模块从内侧推入模块腔,靠唇口限位,外面可见 26.4 x 4.8 槽口。
        MAG_OPEN_W, MAG_OPEN_H = 22.0 * 1.2, 4.0 * 1.2
        MAG_POCK_W, MAG_POCK_H = 27.6, 4.4
        MAG_LIP = 2.0
        # 高度对齐 Cone_Case:磁吸口中心到地面 22.5 + 底盖 4.8 = 27.3mm
        MAG_Z = 22.5 + 4.8
        for _mx in (44.0, 194.0):
            p = p.cut(Part.makeBox(MAG_POCK_W, PANEL_T - MAG_LIP, MAG_POCK_H,
                                   Vector(_mx - MAG_POCK_W / 2, y_plate, MAG_Z - MAG_POCK_H / 2)))
            p = p.cut(Part.makeBox(MAG_OPEN_W, 6.0, MAG_OPEN_H,
                                   Vector(_mx - MAG_OPEN_W / 2, y_plate + MAG_LIP,
                                          MAG_Z - MAG_OPEN_H / 2)))
        print("  磁吸接口孔 x2 @ x=44 / 194, z=%.1f(到地面,对齐 CC): 露出开口 %.1f x %.1f, 模块腔 %.1f x %.1f, 唇口 %.1f" % (
            MAG_Z, MAG_OPEN_W, MAG_OPEN_H, MAG_POCK_W, MAG_POCK_H, MAG_LIP))
        p = p.fuse(Part.makeBox(RPI_HX + 20, 13.5, 5.0,
                                Vector(rpi_cx - RPI_HX / 2 - 10, back_in - 12, RPI_Z0 - 5)))
        p = p.fuse(Part.makeBox(RPI_HX + 20, 13.5, 5.0,
                                Vector(rpi_cx - RPI_HX / 2 - 10, back_in - 12, RPI_Z0 + RPI_Wd)))
    p = p.removeSplitter()
    print("%s: %.1f cm3 solids=%d valid=%s" % (tag, p.Volume / 1000, len(p.Solids), p.isValid()))
    return p


panel = make_panel(True, False, "正面面板")
# ---------------- v31 底面盖板(可拆,圆角,4 卡爪勾内侧台阶)----------------
BP_CLR = 0.5
BX0, BY0 = BOT_OX0 + BP_CLR, BOT_OY0 + BP_CLR
BP_W, BP_D = (BOT_OX1 - BOT_OX0) - 2 * BP_CLR, (BOT_OY1 - BOT_OY0) - 2 * BP_CLR
BP_R = BOT_R - BP_CLR
BP_T, BP_RIM_T, BP_RIM_H, BP_BARB = 6.0, 3.0, 9.0, 1.0
bpanel = make_panel(False, True, "背面面板(含树莓派支架)")
bpanel_b = Part.makeBox(BP_W, BP_D, BP_T, Vector(BX0, BY0, 0.0))
for (_cx, _cy, _dx, _dy) in ((BX0, BY0, BP_R, BP_R), (BX0 + BP_W - BP_R, BY0, 0.0, BP_R),
                             (BX0, BY0 + BP_D - BP_R, BP_R, 0.0),
                             (BX0 + BP_W - BP_R, BY0 + BP_D - BP_R, 0.0, 0.0)):
    _cb = Part.makeBox(BP_R, BP_R, BP_T + 2.0, Vector(_cx, _cy, -1.0))
    _cc = Part.makeCylinder(BP_R, BP_T + 2.0, Vector(_cx + _dx, _cy + _dy, -1.0), Vector(0, 0, 1))
    bpanel_b = bpanel_b.cut(_cb.cut(_cc))
# 环形卡边(3mm 宽,向机内 9mm)
_ring = Part.makeBox(BP_W, BP_D, BP_RIM_H, Vector(BX0, BY0, BP_T))
_ring = _ring.cut(Part.makeBox(BP_W - 2 * BP_RIM_T, BP_D - 2 * BP_RIM_T, BP_RIM_H + 2.0,
                               Vector(BX0 + BP_RIM_T, BY0 + BP_RIM_T, BP_T - 1.0)))
_RB = 9.0
for (_cx, _cy, _dx, _dy) in ((BX0 - 0.5, BY0 - 0.5, _RB, _RB),
                             (BX0 + BP_W + 0.5 - _RB, BY0 - 0.5, 0.0, _RB),
                             (BX0 - 0.5, BY0 + BP_D + 0.5 - _RB, _RB, 0.0),
                             (BX0 + BP_W + 0.5 - _RB, BY0 + BP_D + 0.5 - _RB, 0.0, 0.0)):
    _cb = Part.makeBox(_RB, _RB, BP_RIM_H + 2.0, Vector(_cx, _cy, BP_T - 1.0))
    _cc = Part.makeCylinder(_RB, BP_RIM_H + 2.0, Vector(_cx + _dx, _cy + _dy, BP_T - 1.0), Vector(0, 0, 1))
    _ring = _ring.cut(_cb.cut(_cc))
bpanel_b = bpanel_b.fuse(_ring)
# 4 个卡爪(向外凸 1.0mm;上方 45 度导入斜面,下方平肩勾住外壳内侧台阶)
for _sx in (-1, 1):
    for _by in (BY0 + 30.0, BY0 + BP_D - 30.0):
        _xb = BX0 if _sx < 0 else (BX0 + BP_W)
        prof = [(0.0, BP_T), (BP_BARB, BP_T), (BP_BARB, BP_T + BP_RIM_H - 2.0), (0.0, BP_T + BP_RIM_H - 3.0)]
        poly = [Vector(_xb + _sx * px, _by, pz) for px, pz in prof]
        poly.append(poly[0])
        bpanel_b = bpanel_b.fuse(Part.Face(Part.Wire(Part.makePolygon(poly))).extrude(Vector(0, 3.0, 0)))
        _ty = BY0 + 1.5 if _sx < 0 else BY0 + BP_D - 3.5
        bpanel_b = bpanel_b.cut(Part.makeBox(16.0, 2.5, BP_RIM_H + 2.0, Vector(_by - 8.0, _ty, BP_T - 1.0)))
# 撬槽(底面靠前边缘,便于撬开)
bpanel_b = bpanel_b.cut(Part.makeBox(34.0, 1.5, 5.0, Vector(W / 2 - 17, BY0 - 0.5, -1.0)))
bpanel_b = bpanel_b.cut(Part.makeBox(30.0, 4.0, 4.0, Vector(W / 2 - 15, BY0 - 0.5, -1.0)))
bpanel_b = bpanel_b.removeSplitter()
print("底面盖板: %.1f cm3 solids=%d valid=%s" % (bpanel_b.Volume / 1000, len(bpanel_b.Solids), bpanel_b.isValid()))

# ---------------- 走线槽盖(3 个弹性卡扣)----------------
cvg = sbox(45.0, PLATE_L - 1.0 - CH_U0, COVER_T, CH_X0, CH_U0, back=PLATE_T - COVER_T)
cvg = do_fillet(cvg, COVER_R, [e for e in cvg.Edges if abs(e.Length - COVER_T) < 0.01], "槽盖四角")
for i in range(CLIP_N):
    uy = CH_U0 + (PLATE_L - 1.0 - CH_U0) * (i + 1) / (CLIP_N + 1)
    prof = [(0.0, CLIP_D0), (CLIP_W, CLIP_D0 + CLIP_W), (CLIP_W, CLIP_D1), (0.0, CLIP_D1)]
    cvg = cvg.fuse(sprism(prof, uy - 6.0, uy + 6.0, CH_X0 + 45.0 - 0.05))
    # 卡扣两侧开弹性槽(让卡扣成为可回弹的悬臂),槽沿厚度方向贯通
    for _s in (-1, 1):
        cvg = cvg.cut(sbox(16.0, 2.0, COVER_T - CLIP_T, CH_X0 + 45.0 - 16.0, uy + _s * 7.0 - 1.0, back=0.0))
# 下端固定插舌(与外层同厚 2.5,插入壳体插舌槽 —— 第 6 处固定)
cvg = cvg.fuse(sbox(45.0, 4.5, 2.5, CH_X0, CH_U0 - 3.0, back=0.0))   # 端部固定舌:多伸 1.5mm 与本体重叠,确保融合
# 左面敞开 + 底面挖空 + 加强筋

cvg = cvg.cut(sbox(38.0, PLATE_L - 1.0 - CH_U0 - 6.0, COVER_T - COVER_FLOOR, CH_X0 - 1.0, CH_U0 + 3.0, back=0.0))
# 右壁掏薄:8mm -> 3.0mm(原 1.5mm 过软,卡扣间"竖棍"受力会弯)
# 竖棍绕竖向弯曲惯性矩 13.5 x t^3/12:1.5mm 时 3.8 mm^4 -> 3.0mm 时 30.4 mm^4(刚度 x8)
cvg = cvg.cut(sbox(5.0, PLATE_L - 1.0 - CH_U0, COVER_T - COVER_FLOOR, CH_X0 + 45.0 - 8.0, CH_U0, back=0.0))
# 加强筋:下端 24mm(到第二根卡扣起始)保留 6 条纵筋(间距 6,净间隙 3.2);其余留空走线
_RIB_W, _RIB_H = 3.0, 3.0
_rib_back = -(COVER_T - COVER_FLOOR - _RIB_H)
_rib_u0 = CH_U0 + 1.5
_rib_u1 = CH_U0 + 1.5 + 24.0
for _rx in (CH_X0 + 1.6, CH_X0 + 7.8, CH_X0 + 14.0, CH_X0 + 20.2, CH_X0 + 26.4, CH_X0 + 32.6):
    cvg = cvg.fuse(sbox(_RIB_W, _rib_u1 - _rib_u0, _RIB_H, _rx, _rib_u0, back=_rib_back))
print("加强筋:6 条(3x3,长 %.0f),无封盖" % (_rib_u1 - _rib_u0))

cvg = cvg.cut(sbox(18.0, 10.0, 3.0, CH_X0 + 13.5, PLATE_L - 1.0 - CH_U0 - 4.5 + CH_U0, back=PLATE_T + 1.0))
# v32 修正:按壳体 R25 竖向圆角给槽盖右侧裁形 —— 消除圆角处的长条缝
_pr = Part.makeBox(W, DEP, HT + 40.0, Vector(0, 0, -20))
for _sx, _sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
    _cx = 25.0 if _sx < 0 else (W - 25.0)
    _cy = 25.0 if _sy < 0 else (DEP - 25.0)
    _bx0 = 0.0 if _sx < 0 else _cx
    _bx1 = _cx if _sx < 0 else W
    _by0 = 0.0 if _sy < 0 else _cy
    _by1 = _cy if _sy < 0 else DEP
    _b = Part.makeBox(_bx1 - _bx0, _by1 - _by0, HT + 40.0, Vector(_bx0, _by0, -20))
    _c = Part.makeCylinder(25.0, HT + 60.0, Vector(_cx, _cy, -30), Vector(0, 0, 1))
    _pr = _pr.cut(_b.cut(_c))
cvg = cvg.common(_pr)
print("  [OK] 槽盖已按 R25 裁形(消除圆角处长条缝)")
cvg = cvg.removeSplitter()
print("走线槽盖: %.1f cm3 solids=%d valid=%s" % (cvg.Volume / 1000, len(cvg.Solids), cvg.isValid()))

ob = doc.addObject("Part::Feature", "Shell"); ob.Shape = body
op = doc.addObject("Part::Feature", "Front_Cover"); op.Shape = panel
ob2 = doc.addObject("Part::Feature", "Back_Cover"); ob2.Shape = bpanel
ob3 = doc.addObject("Part::Feature", "Bottom_Cover"); ob3.Shape = bpanel_b
oc = doc.addObject("Part::Feature", "Channel_Cover"); oc.Shape = cvg
doc.recompute()
doc.saveAs(os.path.join(D, "Host_Shell.FCStd"))
import MeshPart, Import
for nm, shp in (("Host_Shell", body), ("Front_Cover", panel),
                ("Back_Cover", bpanel), ("Bottom_Cover", bpanel_b), ("Channel_Cover", cvg)):
    m = MeshPart.meshFromShape(Shape=shp, LinearDeflection=0.1, AngularDeflection=0.4, Relative=False)
    m.write(os.path.join(D, "%s.stl" % nm))
    tmp = doc.addObject("Part::Feature", "_tmp"); tmp.Shape = shp
    Import.export([tmp], os.path.join(D, "%s.step" % nm))
    doc.removeObject(tmp.Name)
    print("%-14s %.1f x %.1f x %.1f | %.1f cm3 | 面片 %d" % (
        nm, shp.BoundBox.XLength, shp.BoundBox.YLength, shp.BoundBox.ZLength,
        shp.Volume / 1000, m.CountFacets))
sys.stdout.flush()
os._exit(0)
