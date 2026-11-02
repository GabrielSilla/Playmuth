"""Check the Playmuth icon (512 x 512 RGB PNG) and copy it to sce_sys/icon0.png."""
import struct
import zlib
from pathlib import Path

root = Path(__file__).resolve().parent
icon = (root / "assets/playmuth-icon.png").read_bytes()
assert icon[:8] == b"\x89PNG\r\n\x1a\n", "playmuth-icon.png is not a PNG"
length, kind = struct.unpack(">I4s", icon[8:16])
assert kind == b"IHDR" and length == 13
width, height, depth, color = struct.unpack(">IIBB", icon[16:26])
assert struct.unpack(">I", icon[29:33])[0] == zlib.crc32(icon[12:29])
# The PS5 menu wants a square 512 px icon; no alpha channel (color type 2).
assert (width, height, depth, color) == (512, 512, 8, 2), (width, height, depth, color)
(root / "sce_sys/icon0.png").write_bytes(icon)
