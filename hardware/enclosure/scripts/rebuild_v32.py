# 由当前 STL 重建 Blender 工程(五件)并渲染装配图
import bpy, os, math, mathutils
SRC = r"C:\Users\SomaRia\Documents\deepseek-harness\default-workspace\host-shell"
PRJ = r"C:\Users\SomaRia\Desktop\pj\小爱风格主机外壳_Blender"
PARTS = [("Shell", "Host_Shell.stl", (0.62, 0.65, 0.70, 1.0)),
         ("Front_Cover", "Front_Cover.stl", (0.30, 0.47, 0.72, 1.0)),
         ("Channel_Cover", "Channel_Cover.stl", (0.85, 0.42, 0.22, 1.0)),
         ("Back_Cover", "Back_Cover.stl", (0.35, 0.62, 0.42, 1.0)),
         ("Bottom_Cover", "Bottom_Cover.stl", (0.55, 0.35, 0.65, 1.0))]

bpy.ops.wm.read_factory_settings(use_empty=True)


def imp(p):
    for m, f in (("wm", "stl_import"), ("import_mesh", "stl")):
        try:
            getattr(getattr(bpy.ops, m), f)(filepath=p)
            return True
        except Exception:
            pass
    return False


asm = bpy.data.collections.new("装配")
bpy.context.scene.collection.children.link(asm)
objs = []
for name, fn, col in PARTS:
    before = set(bpy.data.objects)
    if not imp(os.path.join(SRC, fn)):
        print("导入失败", fn)
        continue
    o = [x for x in bpy.data.objects if x not in before][0]
    o.name = name
    for c in list(o.users_collection):
        c.objects.unlink(o)
    asm.objects.link(o)
    m = bpy.data.materials.new(name + "_mat")
    m.use_nodes = True
    bs = m.node_tree.nodes.get("Principled BSDF")
    if bs:
        bs.inputs["Base Color"].default_value = col
    o.data.materials.append(m)
    objs.append(o)
    print("  %-14s 面片 %d" % (name, len(o.data.polygons)))

pts = []
for o in objs:
    pts += [o.matrix_world @ v.co for v in o.data.vertices]
mn = mathutils.Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
mx = mathutils.Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
ctr = (mn + mx) / 2
diag = (mx - mn).length
cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
bpy.context.scene.collection.objects.link(cam)
bpy.context.scene.camera = cam
cam.data.lens = 55
d = mathutils.Vector((0.72, -0.78, 0.52)).normalized()
cam.location = ctr + d * (diag * 2.3)
cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
s = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", type='SUN'))
s.data.energy = 3.5
s.rotation_euler = (math.radians(50), math.radians(15), math.radians(35))
bpy.context.scene.collection.objects.link(s)
a = bpy.data.objects.new("Area", bpy.data.lights.new("Area", type='AREA'))
a.data.energy = 500000
a.data.size = 400
a.location = ctr + mathutils.Vector((-260, -340, 360))
a.rotation_euler = (math.radians(45), 0, math.radians(-38))
bpy.context.scene.collection.objects.link(a)
w = bpy.data.worlds.new("World")
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.88, 0.89, 0.91, 1.0)
bpy.context.scene.world = w
sc = bpy.context.scene
sc.render.resolution_x = 1600
sc.render.resolution_y = 1200
try:
    sc.render.engine = 'BLENDER_EEVEE_NEXT'
except Exception:
    sc.render.engine = 'BLENDER_EEVEE'
t = bpy.data.texts.new("设计说明")
t.write("""小爱风格主机外壳 v32(Blender 主文件)
238 x 125 x 248.3,壁厚 6,竖角 R25
背面 185x139 卡扣可拆面板(含树莓派 3B 支架,孔距 58x49)
正面 185x139 卡扣可拆面板;底面 206x93 R9 可拆盖板
屏幕盲槽 171x103x13.5;走线区 45x107(R25 裁形)+ 槽盖;右侧壁 10x4 孔 + RJ45 12.68x9.0
开口内侧 2x3 遮光挡边;面板卡边四角 R6.5/R9 贴住开口 -> 缝隙不透光
""")
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(PRJ, "Host_Shell.blend"))
sc.render.filepath = os.path.join(PRJ, "预览_装配.png")
bpy.ops.render.render(write_still=True)
print("完成: 对象 %d" % len(bpy.data.objects))
