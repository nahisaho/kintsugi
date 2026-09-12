import numpy as np
import struct

def pack_rgb(r, g, b):
    rgb_int = (r << 16) | (g << 8) | b
    return struct.unpack('f', struct.pack('I', rgb_int))[0]

def write_pcd(path, points, normals, rgb_float):
    n = len(points)
    with open(path, 'w') as f:
        f.write("# .PCD v0.7 - Point Cloud Data file format\n")
        f.write("VERSION 0.7\n")
        f.write("FIELDS x y z normal_x normal_y normal_z rgb\n")
        f.write("SIZE 4 4 4 4 4 4 4\n")
        f.write("TYPE F F F F F F F\n")
        f.write("COUNT 1 1 1 1 1 1 1\n")
        f.write(f"WIDTH {n}\n")
        f.write("HEIGHT 1\n")
        f.write("VIEWPOINT 0 0 0 1 0 0 0\n")
        f.write(f"POINTS {n}\n")
        f.write("DATA ascii\n")
        for p, nvec in zip(points, normals):
            f.write(f"{p[0]:.6f} {p[1]:.6f} {p[2]:.6f} {nvec[0]:.6f} {nvec[1]:.6f} {nvec[2]:.6f} {rgb_float}\n")

rng = np.random.default_rng(42)

# 想定: 高さ120mm、半径50mmの壺（円柱近似）が4つの破片に割れている。
radius = 50.0
height = 120.0
n_height = 40
n_angle = 60

# 各破片が担当する角度範囲（度）。隙間なく全周をカバーしつつ4分割。
fragments = [
    (0, 95, (180, 140, 110)),    # 素地色(テラコッタ)
    (95, 185, (170, 130, 100)),
    (185, 270, (190, 150, 115)),
    (270, 360, (175, 135, 105)),
]

for idx, (a_start, a_end, color) in enumerate(fragments, start=1):
    points = []
    normals = []
    angles = np.linspace(np.radians(a_start), np.radians(a_end), n_angle)
    heights = np.linspace(0, height, n_height)
    for h in heights:
        for a in angles:
            # ろくろ成形のような緩いテーパー(下がすぼまり、中央が膨らむ壺形状)
            profile = radius * (1.0 - 0.15 * np.cos(np.pi * (h / height)))
            x = profile * np.cos(a)
            y = profile * np.sin(a)
            z = h
            # 表面のノイズ(実スキャンらしさを出すため)
            noise = rng.normal(0, 0.3, size=3)
            points.append((x + noise[0], y + noise[1], z + noise[2]))
            normals.append((np.cos(a), np.sin(a), 0.0))
    rgb_float = pack_rgb(*color)
    out_path = f"demo_data/pottery_fragment_{idx}.pcd"
    write_pcd(out_path, points, normals, rgb_float)
    print(f"wrote {out_path}: {len(points)} points, angle {a_start}-{a_end} deg")
