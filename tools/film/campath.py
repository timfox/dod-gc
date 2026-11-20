"""Keyframed camera paths, the .cam text the engine's cine_play reads.

A Python mirror of xash3d-gc engine/client/cine_path.c: same format, same sampling (cubic Hermite through
smooth keys with zero tangents at the ends of a smooth run, linear, hold), so an animatic shows the move the
engine will make. Keep the two in step; tests/test_film.py pins the shared cases.
"""
from dataclasses import dataclass, field
import math

SMOOTH, LINEAR, HOLD = "smooth", "linear", "hold"
MODES = (SMOOTH, LINEAR, HOLD)
MAX_KEYS = 64  # CINE_MAX_KEYS in the engine


@dataclass
class Key:
    t: float
    o: tuple  # x y z
    a: tuple  # pitch yaw roll (engine convention: positive pitch looks down)
    fov: float = 75.0
    mode: str = SMOOTH


@dataclass
class Path:
    keys: list = field(default_factory=list)

    @property
    def duration(self):
        return self.keys[-1].t if self.keys else 0.0


def _unwrap(prev, cur):
    while cur - prev > 180.0:
        cur -= 360.0
    while cur - prev < -180.0:
        cur += 360.0
    return cur


def _norm(a):
    a = math.fmod(a, 360.0)
    if a > 180.0:
        a -= 360.0
    if a <= -180.0:
        a += 360.0
    return a


def validate(keys):
    """Raises ValueError for what the engine would reject; returns keys with angles unwrapped."""
    if not keys:
        raise ValueError("no keys")
    if len(keys) > MAX_KEYS:
        raise ValueError(f"{len(keys)} keys; the engine takes at most {MAX_KEYS}")
    out = []
    for i, k in enumerate(keys):
        if k.mode not in MODES:
            raise ValueError(f"key {i}: mode {k.mode!r}")
        if not 1.0 <= k.fov <= 170.0:
            raise ValueError(f"key {i}: fov {k.fov} out of 1..170")
        a = tuple(k.a)
        if out:
            if k.t <= out[-1].t:
                raise ValueError(f"key {i}: times must increase")
            a = tuple(_unwrap(p, c) for p, c in zip(out[-1].a, a))
        elif k.t < 0:
            raise ValueError("first key time is negative")
        out.append(Key(float(k.t), tuple(map(float, k.o)), a, float(k.fov), k.mode))
    return out


def parse(text):
    keys, header = [], False
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].split()
        if not line:
            continue
        if line[0] == "cam":
            if line[1:2] != ["1"]:
                raise ValueError(f"line {n}: unsupported version")
            header = True
            continue
        if line[0] != "key":
            raise ValueError(f"line {n}: unknown statement")
        if not header:
            raise ValueError(f"line {n}: key before \"cam 1\" header")
        if len(line) < 9:
            raise ValueError(f"line {n}: key needs t x y z pitch yaw roll fov")
        v = [float(x) for x in line[1:9]]
        keys.append(Key(v[0], tuple(v[1:4]), tuple(v[4:7]), v[7], line[9] if len(line) > 9 else SMOOTH))
    if not header:
        raise ValueError("missing \"cam 1\" header")
    return Path(validate(keys))


def format_key(k):
    return (f"key {k.t:.3f} {k.o[0]:.1f} {k.o[1]:.1f} {k.o[2]:.1f} "
            f"{k.a[0]:.2f} {_norm(k.a[1]):.2f} {k.a[2]:.2f} {k.fov:.1f} {k.mode}")


def dumps(path, comment=None):
    lines = ["cam 1"]
    if comment:
        lines += [f"# {c}" for c in comment.splitlines()]
    lines += [format_key(k) for k in path.keys]
    return "\n".join(lines) + "\n"


def _chan(k, c):
    return k.o[c] if c < 3 else k.a[c - 3] if c < 6 else k.fov


def _tangent(keys, i, c):
    if i <= 0 or i >= len(keys) - 1:
        return 0.0
    if keys[i - 1].mode != SMOOTH or keys[i].mode != SMOOTH:
        return 0.0
    return (_chan(keys[i + 1], c) - _chan(keys[i - 1], c)) / (keys[i + 1].t - keys[i - 1].t)


def sample(path, t):
    """(origin, angles, fov) at time t, clamped to the path."""
    keys = path.keys
    if len(keys) == 1 or t <= keys[0].t:
        k = keys[0]
        out = [_chan(k, c) for c in range(7)]
    elif t >= keys[-1].t:
        k = keys[-1]
        out = [_chan(k, c) for c in range(7)]
    else:
        i = 0
        while i < len(keys) - 2 and t >= keys[i + 1].t:
            i += 1
        k0, k1 = keys[i], keys[i + 1]
        dt = k1.t - k0.t
        s = (t - k0.t) / dt
        out = []
        for c in range(7):
            p0, p1 = _chan(k0, c), _chan(k1, c)
            if k0.mode == HOLD:
                out.append(p0)
            elif k0.mode == LINEAR:
                out.append(p0 + (p1 - p0) * s)
            else:
                m0, m1 = _tangent(keys, i, c) * dt, _tangent(keys, i + 1, c) * dt
                s2, s3 = s * s, s * s * s
                out.append((2 * s3 - 3 * s2 + 1) * p0 + (s3 - 2 * s2 + s) * m0
                           + (-2 * s3 + 3 * s2) * p1 + (s3 - s2) * m1)
    return tuple(out[0:3]), tuple(_norm(a) for a in out[3:6]), out[6]


def look_angles(eye, target):
    """Engine angles (pitch down-positive, yaw) from eye looking at target."""
    dx, dy, dz = (target[i] - eye[i] for i in range(3))
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = -math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    return (pitch, yaw, 0.0)
