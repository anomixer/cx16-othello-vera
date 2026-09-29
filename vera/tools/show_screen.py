#!/usr/bin/env python3
"""Render a headless-harness BMP as coarse ASCII art so the VERA output can be
inspected in a text log, plus a colour histogram."""
import sys
import numpy as np
from PIL import Image

path = sys.argv[1]
cols = int(sys.argv[2]) if len(sys.argv) > 2 else 100
rows = int(sys.argv[3]) if len(sys.argv) > 3 else 36

im = Image.open(path).convert("RGB")
w, h = im.size
a = np.array(im, dtype=np.float32)
lum = a @ np.array([0.299, 0.587, 0.114], dtype=np.float32)

# average pool to cols x rows
ys = np.linspace(0, h, rows + 1).astype(int)
xs = np.linspace(0, w, cols + 1).astype(int)
print(f"{path}: {w}x{h}")
ramp = " .:-=+*#%@"
for r in range(rows):
    line = ""
    for c in range(cols):
        block = lum[ys[r]:ys[r + 1], xs[c]:xs[c + 1]]
        v = block.mean() / 255.0
        line += ramp[min(len(ramp) - 1, int(v * len(ramp)))]
    print(line)

flat = a.reshape(-1, 3)
quant = (flat // 32) * 32
keys, counts = np.unique(quant, axis=0, return_counts=True)
order = np.argsort(-counts)[:10]
print("top colours (r,g,b) x count:",
      [(tuple(int(v) for v in keys[i]), int(counts[i])) for i in order])
