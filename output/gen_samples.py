#!/usr/bin/env python3
# Generates the small contour-container samples used by the acceptance run.
# Format (little-endian), see Include/tessio.h:
#   u32 magic "TEC1" | u16 version=1 | u16 reserved | u32 contourCount
#   per contour: u32 vertexCount | u32 flags (bit0=hole) | vertexCount*(f32 x,y)
import os
import struct

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "samples")


def container(rings, magic=b"TEC1", version=1):
    out = bytearray()
    out += magic
    out += struct.pack("<HH", version, 0)
    out += struct.pack("<I", len(rings))
    for verts, hole in rings:
        out += struct.pack("<II", len(verts), 1 if hole else 0)
        for x, y in verts:
            out += struct.pack("<ff", x, y)
    return bytes(out)


def write(name, data):
    path = os.path.join(OUT, name)
    with open(path, "wb") as f:
        f.write(data)
    print("%s: %d bytes" % (name, len(data)))


os.makedirs(OUT, exist_ok=True)

QUAD = [(0.0, 0.0), (3.0, 0.0), (3.0, 3.0), (0.0, 3.0)]
HOLE = [(1.0, 1.0), (2.0, 1.0), (2.0, 2.0), (1.0, 2.0)]

# Valid inputs.
write("valid_quad.t2c", container([(QUAD, False)]))
write("valid_hole.t2c", container([(QUAD, False), (HOLE, True)]))

# Structure incomplete: header promises 4 vertices, buffer ends after 2.
truncated = container([(QUAD, False)])
write("bad_truncated.t2c", truncated[: 12 + 8 + 2 * 8])

# Coordinate out of range: one x is 1e30 (valid range is +/-2**23).
BAD_QUAD = [(1e30, 0.0), (3.0, 0.0), (3.0, 3.0), (0.0, 3.0)]
write("bad_coord.t2c", container([(BAD_QUAD, False)]))

# Ring nesting anomaly: hole without any preceding outer ring.
write("bad_nesting_orphan.t2c", container([(HOLE, True)]))

# Ring nesting anomaly: hole lies outside its parent outer ring.
FAR_HOLE = [(10.0, 10.0), (20.0, 10.0), (20.0, 20.0), (10.0, 20.0)]
write("bad_nesting_outside.t2c", container([(QUAD, False), (FAR_HOLE, True)]))

# Not a container at all.
write("bad_magic.t2c", container([(QUAD, False)], magic=b"JUNK"))
