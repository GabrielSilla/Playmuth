"""Rasterize the Playmuth symbol SVG as a 512px RGB title icon (even-odd fill)."""
import re
import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

root = Path(__file__).resolve().parent
svg = ET.parse(root / "assets/playmuth-symbol.svg").getroot()
assert svg.attrib["viewBox"] == "0 0 512 512"
polygons = []
for node in svg.iter():
    tag = node.tag.rsplit("}", 1)[-1]
    if tag == "polygon":
        values = [float(x) for x in re.findall(r"[-\d.]+", node.attrib["points"])]
        polygons.append(list(zip(values[::2], values[1::2])))
    elif tag == "path":
        tokens = iter(re.findall(r"[MLCZ]|-?\d+(?:\.\d+)?", node.attrib["d"]))
        points = []
        for token in tokens:
            if token in ("M", "L"):
                points.append((float(next(tokens)), float(next(tokens))))
            elif token == "C":
                a = points[-1]
                b, c, d = [(float(next(tokens)), float(next(tokens))) for _ in range(3)]
                for i in range(1, 33):
                    t = i / 32
                    points.append(tuple((1-t)**3*a[k] + 3*(1-t)**2*t*b[k] +
                                        3*(1-t)*t*t*c[k] + t**3*d[k] for k in (0, 1)))
            elif token != "Z":
                raise ValueError("Unsupported SVG path command: " + token)
        polygons.append(points)
assert polygons
left = int(min(x for p in polygons for x, _ in p)) - 1
right = int(max(x for p in polygons for x, _ in p)) + 1
top = int(min(y for p in polygons for _, y in p)) - 1
bottom = int(max(y for p in polygons for _, y in p)) + 1


def contains(points, x, y):
    inside = False
    a = points[-1]
    for b in points:
        if (a[1] > y) != (b[1] > y) and x < (b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:
            inside = not inside
        a = b
    return inside


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


rows = bytearray()
background, foreground = (9, 13, 17), (245, 247, 250)
for y in range(512):
    rows.append(0)
    for x in range(512):
        covered = 0
        if left <= x <= right and top <= y <= bottom:
            for sy in range(4):
                for sx in range(4):
                    if sum(contains(p, x+(sx+.5)/4, y+(sy+.5)/4) for p in polygons) % 2:
                        covered += 1
        rows.extend(round(a+(b-a)*covered/16) for a, b in zip(background, foreground))
png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", struct.pack(">IIBBBBB", 512, 512, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
Path(__file__).with_name("sce_sys").joinpath("icon0.png").write_bytes(png)
