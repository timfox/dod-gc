"""The engine's frame stamp (xash3d-gc engine/client/cine_path.c Cine_StampWord): read it off a captured frame.

20 square cells along the top edge from the left, each 1/80 of the frame width: cell 0 white, cells 1..17 the
frame number LSB first, cell 18 even parity, cell 19 black.
"""
BITS = 20
DATA = BITS - 3
MASK = (1 << DATA) - 1


def _parity(v):
    return bin(v).count("1") & 1


def word(frame):
    data = frame & MASK
    return 1 | (data << 1) | (_parity(data) << (BITS - 2))


def frame_of(w):
    if not (w & 1) or (w >> (BITS - 1)) & 1:
        return None
    data = (w >> 1) & MASK
    if (w >> (BITS - 2)) & 1 != _parity(data):
        return None
    return data


def cell_size(width):
    return width / 80.0


def read(img):
    """Frame number stamped on a PIL image, or None when the frame carries no stamp."""
    from PIL import ImageStat
    g = img.convert("L")
    cell = cell_size(g.width)
    y = int(cell / 2)
    w = 0
    for i in range(BITS):
        # average a small square in the cell's middle so scaling and compression blur do not flip a bit
        cx = int(i * cell + cell / 2)
        r = max(1, int(cell / 4))
        v = ImageStat.Stat(g.crop((cx - r, y - r + 1, cx + r, y + r))).mean[0]
        if v > 170:
            w |= 1 << i
        elif v > 85:
            return None  # mid grey: picture, not a stamp
    return frame_of(w)


def draw(img, frame):
    """Draw a stamp like the engine does (for tests and synthetic frames)."""
    from PIL import ImageDraw
    d = ImageDraw.Draw(img)
    cell = cell_size(img.width)
    w = word(frame)
    for i in range(BITS):
        v = 255 if (w >> i) & 1 else 0
        d.rectangle([round(i * cell), 0, round((i + 1) * cell) - 1, round(cell) - 1], fill=(v, v, v))
    return img
