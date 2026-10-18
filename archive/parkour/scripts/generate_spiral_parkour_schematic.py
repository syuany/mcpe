#!/usr/bin/env python3
"""
Generate a High-Altitude Spiral Parkour Course Schematic for Minecraft 1.12.2 (Sprint 4).
Format: Standard WorldEdit .schematic (NBT + Gzip)
"""

import gzip
import io
import math
import os
import struct

WIDTH = 64
HEIGHT = 64
LENGTH = 64

TAG_END = 0
TAG_SHORT = 2
TAG_BYTE_ARRAY = 7
TAG_STRING = 8
TAG_LIST = 9
TAG_COMPOUND = 10

def write_string(f, s: str):
    data = s.encode('utf-8')
    f.write(struct.pack('>H', len(data)))
    f.write(data)

def write_tag_header(f, tag_type: int, name: str):
    f.write(struct.pack('>B', tag_type))
    write_string(f, name)

def main():
    total_blocks = WIDTH * HEIGHT * LENGTH
    blocks = bytearray(total_blocks)
    data = bytearray(total_blocks)

    def set_block(x, y, z, block_id, meta=0):
        if 0 <= x < WIDTH and 0 <= y < HEIGHT and 0 <= z < LENGTH:
            idx = (y * LENGTH + z) * WIDTH + x
            blocks[idx] = block_id & 0xFF
            data[idx] = meta & 0x0F

    cx = 32
    cz = 32
    base_y = 32

    # 1. Start Platform (5x5 Quartz at center 32, 32, 32)
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            set_block(cx + dx, base_y, cz + dz, 155, 0)  # Quartz Block
    # Gold Start Pad at (32, 32, 35)
    for dx in range(-1, 2):
        set_block(cx + dx, base_y, cz + 3, 41, 0)       # Gold Block (Start line)

    # 2. Spiral Parkour Pads climbing from Y=33 to Y=48
    steps = 16
    for i in range(steps):
        angle = (i / float(steps)) * 2.0 * math.pi
        r = 10.0
        px = int(round(cx + r * math.sin(angle)))
        pz = int(round(cz + r * math.cos(angle)))
        py = base_y + 1 + i
        bid = 41 if (i == 7) else 155  # Step 7 is Checkpoint 1 (Gold Block)
        for dx in range(-1, 2):
            for dz in range(-1, 2):
                set_block(px + dx, py, pz + dz, bid, 0)

    # 3. Finish Platform (5x5 Sea Lantern & Emerald at Y=50, center 32, 50, 32)
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            set_block(cx + dx, base_y + 18, cz + dz, 133 if (abs(dx) <= 1 and abs(dz) <= 1) else 169, 0)

    bio = io.BytesIO()
    write_tag_header(bio, TAG_COMPOUND, "Schematic")
    write_tag_header(bio, TAG_SHORT, "Width")
    bio.write(struct.pack('>h', WIDTH))
    write_tag_header(bio, TAG_SHORT, "Height")
    bio.write(struct.pack('>h', HEIGHT))
    write_tag_header(bio, TAG_SHORT, "Length")
    bio.write(struct.pack('>h', LENGTH))
    write_tag_header(bio, TAG_STRING, "Materials")
    write_string(bio, "Alpha")
    write_tag_header(bio, TAG_BYTE_ARRAY, "Blocks")
    bio.write(struct.pack('>i', len(blocks)))
    bio.write(blocks)
    write_tag_header(bio, TAG_BYTE_ARRAY, "Data")
    bio.write(struct.pack('>i', len(data)))
    bio.write(data)
    write_tag_header(bio, TAG_LIST, "Entities")
    bio.write(struct.pack('>B', TAG_COMPOUND))
    bio.write(struct.pack('>i', 0))
    write_tag_header(bio, TAG_LIST, "TileEntities")
    bio.write(struct.pack('>B', TAG_COMPOUND))
    bio.write(struct.pack('>i', 0))
    bio.write(struct.pack('>B', TAG_END))

    os.makedirs("assets/maps/parkour", exist_ok=True)
    out_path = "assets/maps/parkour/spiral.schematic"
    with gzip.open(out_path, 'wb') as gf:
        gf.write(bio.getvalue())
    print(f"Generated {out_path} ({os.path.getsize(out_path)} bytes)")

if __name__ == "__main__":
    main()
