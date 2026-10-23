#!/usr/bin/env python3
"""Build README artwork from committed binary STL files using NumPy and Pillow.

Run from any directory: python3 tools/generate_readme_art.py
The drawings are shaded mesh previews, not assembly or manufacturing validation.
"""
import base64
import io
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/assets"
CAD = ROOT / "hardware/enclosure"


def triangles(name, offset=(0, 0, 0)):
    raw = (CAD / name).read_bytes()
    count = struct.unpack_from("<I", raw, 80)[0]
    if len(raw) != 84 + count * 50:
        raise ValueError(f"Not a binary STL: {name}")
    result = []
    for i in range(count):
        values = struct.unpack_from("<12fH", raw, 84 + i * 50)
        points = [tuple(values[3+j*3+k] + offset[k] for k in range(3)) for j in range(3)]
        result.append(points)
    return result


def svg_mesh(mesh, bounds, tint):
    x, y, w, h = bounds
    image = raster_mesh(mesh, (w*2, h*2), tint)
    data = io.BytesIO()
    image.save(data, format="PNG")
    encoded = base64.b64encode(data.getvalue()).decode("ascii")
    return f'<image x="{x}" y="{y}" width="{w}" height="{h}" href="data:image/png;base64,{encoded}"/>'


def raster_mesh(mesh, size, tint):
    """Orthographic depth-buffered rendering; no triangle painter-order artifacts."""
    import numpy as np
    from PIL import Image
    w, h = size
    world = np.asarray(mesh)
    camera = np.asarray([[0.83,0.56,0], [0.31,-0.46,-0.84], [-0.47,0.70,-0.54]])
    p = world @ camera.T
    low, high = p[:,:,:2].min(axis=(0,1)), p[:,:,:2].max(axis=(0,1))
    scale = min((w-4)/(high[0]-low[0]), (h-4)/(high[1]-low[1]))
    p[:,:,:2] = (p[:,:,:2]-low)*scale + (np.asarray([w,h])-(high-low)*scale)/2
    normals = np.cross(world[:,1]-world[:,0], world[:,2]-world[:,0])
    lengths = np.maximum(np.linalg.norm(normals, axis=1), 1e-10)
    lighting = 0.68+0.30*(normals @ np.asarray([-0.35,-0.45,0.82]))/lengths
    colors = np.clip(np.asarray(tint)[None,:]*lighting[:,None],0,255).astype(np.uint8)
    rgba = np.zeros((h,w,4), dtype=np.uint8)
    depth = np.full((h,w), np.inf)
    for tri, color in zip(p, colors):
        x0,y0 = np.maximum(np.floor(tri[:,:2].min(axis=0)).astype(int),0)
        x1,y1 = np.minimum(np.ceil(tri[:,:2].max(axis=0)).astype(int),[w-1,h-1])
        if x1<x0 or y1<y0: continue
        a,b,c = tri
        denominator = (b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(denominator)<1e-10: continue
        yy,xx = np.mgrid[y0:y1+1,x0:x1+1]
        xx,yy = xx+0.5,yy+0.5
        u = ((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denominator
        v = ((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denominator
        t = 1-u-v
        z = u*a[2]+v*b[2]+t*c[2]
        current = depth[y0:y1+1,x0:x1+1]
        mask = (u>=-1e-8)&(v>=-1e-8)&(t>=-1e-8)&(z<current)
        current[mask] = z[mask]
        rgba[y0:y1+1,x0:x1+1][mask] = [*color,255]
    return Image.fromarray(rgba)


def main():
    from PIL import Image, ImageDraw, ImageFont
    OUT.mkdir(parents=True, exist_ok=True)
    lower = triangles("host-shell/stl/Shell_Lower.stl")
    upper = triangles("host-shell/stl/Shell_Upper.stl", (0, 0, 42))
    mesh = lower + upper
    hero = '''<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="650" viewBox="0 0 1200 650" role="img" aria-labelledby="title desc">
<title id="title">Pi Terminal — a modular world, within reach</title>
<desc id="desc">Editorial technical cover with an exploded preview of the repository's actual upper and lower enclosure STL meshes. Separation is for illustration only.</desc>
<defs><pattern id="grid" width="28" height="28" patternUnits="userSpaceOnUse"><circle cx="1" cy="1" r="0.7" fill="#c8d1c7"/></pattern></defs>
<rect width="1200" height="650" rx="24" fill="#f3f3eb"/>
<rect x="585" y="24" width="591" height="602" rx="14" fill="#e7ece4"/>
<rect x="585" y="24" width="591" height="602" rx="14" fill="url(#grid)"/>
<g font-family="Inter,Arial,sans-serif"><text x="48" y="66" font-size="13" letter-spacing="3" fill="#4d6854">HARDWARE × NATIVE SOFTWARE</text>
<path d="M48 102H110" stroke="#a07736" stroke-width="4"/>
<text x="42" y="207" font-size="86" font-weight="700" letter-spacing="-5" fill="#183329">Pi Terminal.</text>
<text x="48" y="264" font-size="30" fill="#28483a">A modular world,</text><text x="48" y="307" font-size="30" fill="#28483a">within reach.</text>
<text x="48" y="378" font-size="16" fill="#5a6c60">Raspberry Pi · ESP32-S3 · Qt / QML</text>
<rect x="48" y="417" width="158" height="35" rx="17" fill="#214c3d"/><text x="127" y="440" text-anchor="middle" font-size="12" letter-spacing="1" fill="#fff">TOUCH-FIRST</text>
<rect x="216" y="417" width="158" height="35" rx="17" fill="#e4dfce"/><text x="295" y="440" text-anchor="middle" font-size="12" letter-spacing="1" fill="#4e4a3e">MODULE-READY</text>
<path d="M48 534H530" stroke="#ccd3c7"/>
<text x="48" y="566" font-size="12" letter-spacing="2" fill="#526758">01 / CONNECT</text><text x="220" y="566" font-size="12" letter-spacing="2" fill="#526758">02 / EXPLORE</text><text x="392" y="566" font-size="12" letter-spacing="2" fill="#526758">03 / BUILD</text>
<text x="610" y="61" font-size="12" letter-spacing="2" fill="#526758">FIG. 01 / ENCLOSURE STUDY</text>
<ellipse cx="875" cy="555" rx="192" ry="32" fill="#cbd5c7"/>
'''+svg_mesh(mesh, (635, 94, 500, 452), (225, 218, 188))+'''
<rect x="610" y="581" width="542" height="1" fill="#c3cdbf"/>
<text x="610" y="609" font-size="12" fill="#526758">Actual STL geometry · separated for display</text><text x="1150" y="609" text-anchor="end" font-size="12" fill="#526758">HOST SHELL / v32</text>
</g></svg>'''
    (OUT / "terminal-hero.svg").write_text(hero, encoding="utf-8")
    models = [
        ("HOST SHELL", "Upper + lower enclosure", lower+triangles("host-shell/stl/Shell_Upper.stl"), (215,207,178)),
        ("CONE CASE", "Portable enclosure / body", triangles("cone-case/stl/Cone_Case_Body.stl"), (149,187,165)),
        ("POT BASE", "FINAL / threaded tray", triangles("pot-base/stl/Pot_Base_FINAL_内壁3mm_倒角优化.stl"), (216,177,113)),
        ("ONE-PIECE STAND", "Support structure", triangles("stand/stl/Stand.stl"), (153,175,196)),
    ]
    ratio = 2
    image = Image.new("RGB", (1200*ratio, 430*ratio), "#f3f3eb")
    draw = ImageDraw.Draw(image)
    font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
    for i, (title, subtitle, source, tint) in enumerate(models):
        x = 18+i*297
        draw.rounded_rectangle((x*ratio,18*ratio,(x+279)*ratio,412*ratio), radius=18*ratio, fill="#e7ece4")
        draw.ellipse(((x+30)*ratio,283*ratio,(x+249)*ratio,317*ratio), fill="#cfd8cb")
        render = raster_mesh(source, (239*ratio,254*ratio), tint)
        image.paste(render, ((x+20)*ratio,51*ratio), render)
        draw.text(((x+19)*ratio,335*ratio), title, font=ImageFont.truetype(bold, 14*ratio), fill="#183329")
        draw.text(((x+19)*ratio,363*ratio), subtitle, font=ImageFont.truetype(font, 11*ratio), fill="#526758")
        draw.text(((x+19)*ratio,387*ratio), f"{len(source):,} triangles / STL", font=ImageFont.truetype(font, 10*ratio), fill="#526758")
    image.save(OUT / "enclosure-gallery.png", optimize=True)
    print("Generated docs/assets/terminal-hero.svg and enclosure-gallery.png")


if __name__ == "__main__":
    main()
