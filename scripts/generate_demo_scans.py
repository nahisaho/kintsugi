#!/usr/bin/env python3
"""デモ用スキャンデータ(PCD)を生成するスクリプト。

テーパー付き円柱(壺)の表面を、多数の不定形な破片(デフォルト100個)に
分割した点群を生成する。各破片が隣接破片と自然な境界で接するよう、
角度・高さ空間上のジッター付きグリッド種点によるVoronoi分割で破片形状を
決定する(単純な角度スライスではなく、より実際の破損片に近い不定形の
破片群になる)。

使い方:
    python3 scripts/generate_demo_scans.py                # 100破片を生成
    python3 scripts/generate_demo_scans.py --num-fragments 4
"""
import argparse
import struct

import numpy as np


def pack_rgb(r: int, g: int, b: int) -> float:
    rgb_int = (r << 16) | (g << 8) | b
    return struct.unpack('f', struct.pack('I', rgb_int))[0]


def write_pcd(path, points, normals, colors):
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
        for p, nvec, rgb_float in zip(points, normals, colors):
            f.write(
                f"{p[0]:.6f} {p[1]:.6f} {p[2]:.6f} "
                f"{nvec[0]:.6f} {nvec[1]:.6f} {nvec[2]:.6f} {rgb_float}\n"
            )


def generate(num_fragments: int, out_dir: str, seed: int = 42,
             points_per_fragment_target: int = 250) -> None:
    rng = np.random.default_rng(seed)

    radius = 50.0
    height = 120.0

    # 破片1個あたりの目標点数から、全体のサンプリング密度(角度・高さ方向の
    # 分割数)を概算する。
    total_points_target = num_fragments * points_per_fragment_target
    n_angle = max(int(np.sqrt(total_points_target * 2.0 * np.pi / height * radius)), 20)
    n_height = max(int(total_points_target / n_angle), 10)

    angles = np.linspace(0, 2 * np.pi, n_angle, endpoint=False)
    heights = np.linspace(0, height, n_height)
    grid_a, grid_h = np.meshgrid(angles, heights)
    grid_a = grid_a.ravel()
    grid_h = grid_h.ravel()

    # ろくろ成形のような緩いテーパー(下がすぼまり、中央が膨らむ壺形状)。
    profile = radius * (1.0 - 0.15 * np.cos(np.pi * (grid_h / height)))
    noise = rng.normal(0, 0.3, size=(len(grid_a), 3))
    xs = profile * np.cos(grid_a) + noise[:, 0]
    ys = profile * np.sin(grid_a) + noise[:, 1]
    zs = grid_h + noise[:, 2]
    normals = np.stack([np.cos(grid_a), np.sin(grid_a), np.zeros_like(grid_a)], axis=1)

    # 破片形状を決める種点を、角度×高さのジッター付きグリッド上に配置する
    # (単純格子だと破片境界が直線的すぎるため、行ごとの角度オフセットと
    # 座標ジッターで不定形な破片境界を作る)。
    n_seed_angle = max(int(round(np.sqrt(num_fragments))), 1)
    n_seed_height = max(int(np.ceil(num_fragments / n_seed_angle)), 1)
    seed_a = []
    seed_h = []
    for row in range(n_seed_height):
        row_offset = rng.uniform(0, 2 * np.pi / n_seed_angle)
        for col in range(n_seed_angle):
            if len(seed_a) >= num_fragments:
                break
            base_a = (2 * np.pi * col / n_seed_angle + row_offset) % (2 * np.pi)
            base_h = (row + 0.5) * height / n_seed_height
            seed_a.append(base_a + rng.normal(0, 0.15))
            seed_h.append(base_h + rng.normal(0, height / n_seed_height * 0.25))
    seed_a = np.array(seed_a[:num_fragments])
    seed_h = np.array(seed_h[:num_fragments])

    # 各点を最も近い種点(Voronoiセル)に割り当てる。角度差は円周上の
    # ラップアラウンドを考慮し、弧長(角度差×半径)として高さと同じ単位で
    # 比較する。
    angle_diff = np.abs((grid_a[:, None] - seed_a[None, :] + np.pi) % (2 * np.pi) - np.pi)
    arc_diff = angle_diff * radius
    height_diff = grid_h[:, None] - seed_h[None, :]
    dist_sq = arc_diff ** 2 + height_diff ** 2
    assignment = np.argmin(dist_sq, axis=1)

    palette = [
        (180, 140, 110), (170, 130, 100), (190, 150, 115), (175, 135, 105),
        (185, 145, 108), (165, 125, 95), (195, 152, 118), (172, 132, 102),
    ]

    written = 0
    num_digits = len(str(num_fragments))
    for frag_idx in range(num_fragments):
        mask = assignment == frag_idx
        count = int(mask.sum())
        if count == 0:
            continue
        pts = np.stack([xs[mask], ys[mask], zs[mask]], axis=1)
        nvecs = normals[mask]
        color = palette[frag_idx % len(palette)]
        rgb_float = pack_rgb(*color)
        colors = [rgb_float] * count
        out_path = f"{out_dir}/pottery_fragment_{frag_idx + 1:0{num_digits}d}.pcd"
        write_pcd(out_path, pts, nvecs, colors)
        written += 1

    print(f"wrote {written} fragment files (requested {num_fragments}) "
          f"from {len(grid_a)} surface points into {out_dir}/")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--num-fragments", type=int, default=100,
                         help="生成する破片数(デフォルト: 100)")
    parser.add_argument("--out-dir", default="demo_data",
                         help="出力先ディレクトリ(デフォルト: demo_data)")
    parser.add_argument("--seed", type=int, default=42, help="乱数シード")
    args = parser.parse_args()
    generate(args.num_fragments, args.out_dir, args.seed)


if __name__ == "__main__":
    main()
