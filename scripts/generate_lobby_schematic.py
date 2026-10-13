#!/usr/bin/env python3
"""
Generate a classic Floating Island Fantasy Lobby Schematic for Minecraft 1.12.2.
Format: Standard WorldEdit .schematic (NBT + Gzip)
"""

import gzip
import struct
import math
import os

WIDTH = 96
HEIGHT = 48
LENGTH = 96

# NBT Tag IDs
TAG_END = 0
TAG_BYTE = 1
TAG_SHORT = 2
TAG_INT = 3
TAG_LONG = 4
TAG_FLOAT = 5
TAG_DOUBLE = 6
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

    cx = WIDTH // 2   # 48
    cz = LENGTH // 2  # 48
    surface_y = 32

    # 1. Underside inverted cone (stone & dirt)
    # y from 16 to 31
    for y in range(16, surface_y):
        layer_progress = (y - 16) / float(surface_y - 16) # 0.0 to 1.0
        r = 3.0 + layer_progress * 25.0
        for x in range(WIDTH):
            for z in range(LENGTH):
                dx = x - cx
                dz = z - cz
                dist = math.sqrt(dx * dx + dz * dz)
                if dist <= r:
                    if y >= surface_y - 2:
                        set_block(x, y, z, 3) # Dirt
                    else:
                        set_block(x, y, z, 1) # Stone

    # 2. Surface layer (Y = 32)
    island_radius = 28.0
    for x in range(WIDTH):
        for z in range(LENGTH):
            dx = x - cx
            dz = z - cz
            dist = math.sqrt(dx * dx + dz * dz)
            if dist <= island_radius:
                if dist <= 10.0:
                    # Central Quartz plaza
                    if int(dist) % 4 == 0:
                        set_block(x, surface_y, z, 169) # Sea Lantern
                    else:
                        set_block(x, surface_y, z, 155, 0) # Quartz Block
                elif dist <= 14.0:
                    # Stone Brick inner ring
                    set_block(x, surface_y, z, 98, 0) # Stone Bricks
                elif dist <= 24.0:
                    # Grass ring
                    set_block(x, surface_y, z, 2) # Grass Block
                else:
                    # Outer Cobblestone rim
                    set_block(x, surface_y, z, 4) # Cobblestone

    # 3. Central Fountain (at cx, cz)
    # Center pool: Y = 33..35
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            if abs(dx) == 2 or abs(dz) == 2:
                set_block(cx + dx, surface_y + 1, cz + dz, 155, 0) # Quartz rim
            else:
                set_block(cx + dx, surface_y + 1, cz + dz, 9) # Water
    # Central pillar
    for y in range(surface_y + 1, surface_y + 5):
        set_block(cx, y, cz, 155, 2) # Pillar Quartz
    set_block(cx, surface_y + 5, cz, 169) # Sea Lantern on top
    set_block(cx, surface_y + 6, cz, 9)   # Water source cascading down

    # 4. Portals in 4 Cardinal directions (North, South, East, West)
    portal_offsets = [
        (0, -18, "North"), # North
        (0, 18, "South"),  # South
        (18, 0, "East"),   # East
        (-18, 0, "West")   # West
    ]

    for px_off, pz_off, _ in portal_offsets:
        px = cx + px_off
        pz = cz + pz_off
        # Portal platform (5x5 Quartz)
        for dx in range(-2, 3):
            for dz in range(-2, 3):
                set_block(px + dx, surface_y, pz + dz, 98, 0) # Stone Bricks
                if abs(dx) == 2 and abs(dz) == 2:
                    set_block(px + dx, surface_y + 1, pz + dz, 169) # Sea Lantern

        # Portal Frame (4 wide, 5 high)
        if px_off == 0: # North/South: frame spans X
            for fx in range(-1, 3): # 4 wide
                for fy in range(1, 6): # 5 high
                    if fx in (-1, 2) or fy in (1, 5):
                        set_block(px + fx, surface_y + fy, pz, 49) # Obsidian
                    else:
                        set_block(px + fx, surface_y + fy, pz, 90) # Portal
        else: # East/West: frame spans Z
            for fz in range(-1, 3):
                for fy in range(1, 6):
                    if fz in (-1, 2) or fy in (1, 5):
                        set_block(px, surface_y + fy, pz + fz, 49) # Obsidian
                    else:
                        set_block(px, surface_y + fy, pz + fz, 90) # Portal

    # 5. Parkour stepping stones (floating blocks around Western side)
    # 8 stepping stones spiraling up
    parkour_coords = [
        (cx - 22, surface_y + 1, cz - 4, 155),
        (cx - 24, surface_y + 2, cz - 1, 155),
        (cx - 25, surface_y + 3, cz + 3, 41), # Gold block checkpoint
        (cx - 23, surface_y + 4, cz + 7, 155),
        (cx - 20, surface_y + 5, cz + 10, 155),
        (cx - 16, surface_y + 6, cz + 13, 155),
        (cx - 12, surface_y + 7, cz + 15, 41),
        (cx - 8,  surface_y + 8, cz + 16, 169), # Finish lantern
    ]
    for kx, ky, kz, bid in parkour_coords:
        set_block(kx, ky, kz, bid, 0)

    # 6. Encode into Gzipped NBT
    raw_nbt = bytearray()
    import io
    bio = io.BytesIO()

    # Root Compound: "Schematic"
    write_tag_header(bio, TAG_COMPOUND, "Schematic")

    # Short: Width
    write_tag_header(bio, TAG_SHORT, "Width")
    bio.write(struct.pack('>h', WIDTH))

    # Short: Height
    write_tag_header(bio, TAG_SHORT, "Height")
    bio.write(struct.pack('>h', HEIGHT))

    # Short: Length
    write_tag_header(bio, TAG_SHORT, "Length")
    bio.write(struct.pack('>h', LENGTH))

    # String: Materials = "Alpha"
    write_tag_header(bio, TAG_STRING, "Materials")
    write_string(bio, "Alpha")

    # Byte_Array: Blocks
    write_tag_header(bio, TAG_BYTE_ARRAY, "Blocks")
    bio.write(struct.pack('>i', len(blocks)))
    bio.write(blocks)

    # Byte_Array: Data
    write_tag_header(bio, TAG_BYTE_ARRAY, "Data")
    bio.write(struct.pack('>i', len(data)))
    bio.write(data)

    # Empty Entities list
    write_tag_header(bio, TAG_LIST, "Entities")
    bio.write(struct.pack('>B', TAG_COMPOUND)) # element type
    bio.write(struct.pack('>i', 0))             # length 0

    # Empty TileEntities list
    write_tag_header(bio, TAG_LIST, "TileEntities")
    bio.write(struct.pack('>B', TAG_COMPOUND))
    bio.write(struct.pack('>i', 0))

    # TAG_End for Root Compound
    bio.write(struct.pack('>B', TAG_END))

    uncompressed_data = bio.getvalue()

    os.makedirs("assets/maps", exist_ok=True)
    out_path = "assets/maps/lobby.schematic"
    with gzip.open(out_path, 'wb') as gf:
        gf.write(uncompressed_data)

    print(f"Generated {out_path} ({len(uncompressed_data)} bytes uncompressed, {os.path.getsize(out_path)} bytes gzipped)")

if __name__ == "__main__":
    main()
