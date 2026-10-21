#!/usr/bin/env python3
"""Pack offline rendered PMA frames into straight-alpha Qt sprite sheets."""
import argparse
import json
from pathlib import Path
from PIL import Image
from PIL import ImageChops, ImageMath
import statistics


def unpremultiply(frame, use_numpy=None):
    """Convert PMA RGBA to straight alpha without Python work per pixel.

    NumPy is an optional fast path (available on the Pi image).  ImageMath is
    the Pillow-only fallback; both operate on whole channels in native code.
    """
    if use_numpy is not False:
        try:
            import numpy as np
        except ImportError:
            if use_numpy:
                raise
        else:
            rgba = np.asarray(frame, dtype=np.uint16)
            alpha = rgba[:, :, 3]
            denominator = np.maximum(alpha, 1)[:, :, None]
            numerator = rgba[:, :, :3] * 255
            quotient = numerator // denominator
            remainder = numerator % denominator
            round_up = (remainder * 2 > denominator) | (
                (remainder * 2 == denominator) & ((quotient & 1) != 0)
            )
            rgb = np.minimum(quotient + round_up, 255).astype(np.uint8)
            rgb[alpha == 0] = 0
            result = np.empty(rgba.shape, dtype=np.uint8)
            result[:, :, :3] = rgb
            result[:, :, 3] = alpha
            return Image.fromarray(result, "RGBA")

    red, green, blue, alpha = frame.split()
    safe_alpha = alpha.point([max(1, value) for value in range(256)])
    visible = alpha.point([0] + [255] * 255)
    channels = []
    for channel in (red, green, blue):
        # Pillow's ImageMath executes this over image bands in native code.
        # Its half-up tie rounding differs from Python round() by at most 1.
        expanded = ImageMath.eval(
            "convert((channel * 255 + alpha / 2) / safe_alpha, 'L')",
            channel=channel, alpha=alpha, safe_alpha=safe_alpha,
        )
        channels.append(ImageChops.multiply(expanded, visible))
    return Image.merge("RGBA", (*channels, alpha))


def pack(frames: Path, output: Path):
    output.mkdir(parents=True, exist_ok=True)
    info = json.loads((frames / "export.json").read_text())
    fw, fh = info["frameWidth"], info["frameHeight"]
    columns=info.get("columns",8)
    idle_heights=[]
    for action in info["actions"]:
        paths = sorted((frames / action).glob("*.png"))
        if not paths:
            raise ValueError(f"No rendered frames: {action}")
        if len(paths) != info["actions"][action]["frames"]:
            raise ValueError("Export frame count mismatch")
        sheet = Image.new("RGBA", (fw * columns, fh * ((len(paths) + columns-1) // columns)))
        foot = 0
        frame_feet=[]
        for index, path in enumerate(paths):
            with Image.open(path) as source:
                frame = source.convert("RGBA")
            # Atlas is PMA; the GL framebuffer is also PMA. Qt's image loader
            # expects straight alpha and premultiplies itself on upload.
            frame = unpremultiply(frame)
            if frame.size != (fw, fh) or not frame.getbbox():
                raise ValueError("Empty or incorrectly sized exported frame")
            bounds=frame.getchannel("A").point(lambda a:255 if a>32 else 0).getbbox()
            if not bounds:raise ValueError("No visible contact pixels in frame")
            foot = max(foot, bounds[3])
            frame_feet.append(bounds[3])
            if action=="idle":idle_heights.append(bounds[3]-bounds[1])
            sheet.paste(frame, ((index % columns) * fw, (index // columns) * fh))
        # optimize=True performs an expensive extra compression search. Level 3
        # keeps PNG lossless while making multi-frame packaging much faster.
        sheet.save(output / f"{action}.png", optimize=False, compress_level=3)
        info["actions"][action]["footY"] = foot
        info["actions"][action]["frameFeet"] = frame_feet
    # Normalize the visible idle silhouette, NOT the padded square canvas or
    # union of special effects/lying poses. Never rescale between actions.
    info["referenceHeight"]=statistics.median(idle_heights)
    info["targetHeight"]=176
    (output / "animations.json").write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n")
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frames", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    pack(args.frames, args.output)


if __name__ == "__main__":
    main()
