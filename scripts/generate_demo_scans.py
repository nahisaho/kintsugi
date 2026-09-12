#!/usr/bin/env python3
"""デモ用スキャンデータ(OBJメッシュ)を生成するスクリプト。

古典的な壺（土器）のシルエット（底部→胴部の膨らみ→肩→首→口縁）を持つ
曲面を、多数の不定形な破片(デフォルト100個)に分割したメッシュ（頂点・
法線・面・破片ごとの色付きマテリアル）を生成する。各破片が隣接破片と
自然な境界で接するよう、角度・高さ空間上のジッター付きグリッド種点による
Voronoi分割で破片形状を決定する(単純な角度スライスではなく、より実際の
破損片に近い不定形の破片群になる)。破片ごとに面(三角形)を持たせることで、
3Dビューア上で各破片のエッジ（メッシュの輪郭線）を表示できる。

使い方:
    python3 scripts/generate_demo_scans.py                # 100破片を生成
    python3 scripts/generate_demo_scans.py --num-fragments 4
"""
import argparse

import numpy as np


def vessel_radius_profile(t: np.ndarray, radius: float) -> np.ndarray:
    """壺（土器）らしいシルエットを表す半径プロファイル。

    tは高さ方向の正規化パラメータ(0=底面, 1=口縁)。底部から膨らんで最大径
    となる胴部を経て、肩部ですぼまり、首部で最も細くなった後、口縁で
    わずかに開く、という古典的な壺の輪郭を、区分アンカー点のCatmull-Rom
    風のコサイン補間で滑らかに表現する。単純な円柱よりも実際の土器の
    シルエットに近い形状になる。
    """
    # (t, 半径/radius) のアンカー点。
    anchors_t = np.array([0.00, 0.06, 0.35, 0.72, 0.88, 1.00])
    anchors_r = np.array([0.32, 0.50, 1.00, 0.55, 0.34, 0.42])

    result = np.zeros_like(t)
    for i in range(len(anchors_t) - 1):
        t0, t1 = anchors_t[i], anchors_t[i + 1]
        r0, r1 = anchors_r[i], anchors_r[i + 1]
        mask = (t >= t0) & (t <= t1 if i == len(anchors_t) - 2 else t < t1)
        local = np.clip((t[mask] - t0) / (t1 - t0), 0.0, 1.0)
        # コサイン補間(区間端で滑らかに接続し、単純な折れ線にならないようにする)。
        smooth = (1.0 - np.cos(local * np.pi)) / 2.0
        result[mask] = r0 + (r1 - r0) * smooth
    return result * radius


def write_obj_fragment(obj_path, mtl_path, mtl_name, points, normals, faces, color):
    """1破片分のOBJ(usemtl参照・vn付き)とMTL(単色マテリアル)を書き出す。

    面(faces)を持たせることで、インポート後に3Dビューア上でエッジ
    (メッシュのワイヤーフレーム)を表示できるようにする。
    """
    mtl_filename = mtl_path.split('/')[-1]
    with open(obj_path, 'w') as f:
        f.write(f"mtllib {mtl_filename}\n")
        for p in points:
            f.write(f"v {p[0]:.6f} {p[1]:.6f} {p[2]:.6f}\n")
        for n in normals:
            f.write(f"vn {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}\n")
        f.write(f"usemtl {mtl_name}\n")
        for face in faces:
            # OBJの頂点・法線インデックスは1始まり。各面は同一頂点の
            # 法線インデックスを使う(v//vn形式)。
            a, b, c = (i + 1 for i in face)
            f.write(f"f {a}//{a} {b}//{b} {c}//{c}\n")

    r, g, b = (c / 255.0 for c in color)
    with open(mtl_path, 'w') as f:
        f.write(f"newmtl {mtl_name}\n")
        f.write(f"Kd {r:.4f} {g:.4f} {b:.4f}\n")
        f.write("Ka 0.0 0.0 0.0\n")
        f.write("Ks 0.0 0.0 0.0\n")
        f.write("d 1.0\n")


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
    grid_a, grid_h = np.meshgrid(angles, heights)  # shape (n_height, n_angle)
    grid_a = grid_a.ravel()
    grid_h = grid_h.ravel()

    # 壺（土器）らしいシルエット（底部→胴部膨らみ→肩→首→口縁）のプロファイル。
    t_param = grid_h / height
    profile = vessel_radius_profile(t_param, radius)

    # 法線は、プロファイル半径の高さ方向の傾き(dr/dh)を考慮し、断面の
    # 半径方向だけでなく表面の傾斜も反映した向きにする(単純な水平法線より
    # 実際の3Dスキャン法線に近い)。
    dt = 1e-4
    profile_plus = vessel_radius_profile(np.clip(t_param + dt, 0.0, 1.0), radius)
    profile_minus = vessel_radius_profile(np.clip(t_param - dt, 0.0, 1.0), radius)
    dr_dh = (profile_plus - profile_minus) / (2.0 * dt * height)

    noise = rng.normal(0, 0.3, size=(len(grid_a), 3))
    xs = profile * np.cos(grid_a) + noise[:, 0]
    ys = profile * np.sin(grid_a) + noise[:, 1]
    zs = grid_h + noise[:, 2]

    radial = np.stack([np.cos(grid_a), np.sin(grid_a), np.zeros_like(grid_a)], axis=1)
    vertical = np.zeros_like(radial)
    vertical[:, 2] = 1.0
    normals = radial - dr_dh[:, None] * vertical
    normals /= np.linalg.norm(normals, axis=1, keepdims=True)

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

    # 角度・高さグリッドの隣接4頂点(1セル)が全て同じ破片に属す場合のみ、
    # そのセルを2枚の三角形に分割して面を張る(破片境界をまたぐセルには
    # 面を張らない。破損片同士の境界は元々滑らかに連続しないため、
    # 実際の破損の見た目としても自然)。
    faces_by_fragment: dict[int, list[tuple[int, int, int]]] = {}
    for i in range(n_height - 1):
        row0 = i * n_angle
        row1 = (i + 1) * n_angle
        for j in range(n_angle):
            j_next = (j + 1) % n_angle
            v00 = row0 + j
            v01 = row0 + j_next
            v10 = row1 + j
            v11 = row1 + j_next
            f00, f01, f10, f11 = (assignment[v00], assignment[v01],
                                   assignment[v10], assignment[v11])
            if f00 == f01 == f10 == f11:
                faces_by_fragment.setdefault(int(f00), []).append((v00, v10, v11))
                faces_by_fragment.setdefault(int(f00), []).append((v00, v11, v01))

    palette = [
        (180, 140, 110), (170, 130, 100), (190, 150, 115), (175, 135, 105),
        (185, 145, 108), (165, 125, 95), (195, 152, 118), (172, 132, 102),
    ]

    written = 0
    num_digits = len(str(num_fragments))
    for frag_idx in range(num_fragments):
        face_list = faces_by_fragment.get(frag_idx)
        if not face_list:
            continue
        global_vertex_ids = sorted({v for face in face_list for v in face})
        local_of_global = {g: local for local, g in enumerate(global_vertex_ids)}
        local_points = np.stack(
            [[xs[g], ys[g], zs[g]] for g in global_vertex_ids])
        local_normals = np.stack([normals[g] for g in global_vertex_ids])
        local_faces = [tuple(local_of_global[v] for v in face) for face in face_list]

        color = palette[frag_idx % len(palette)]
        mtl_name = f"fragment{frag_idx + 1}"
        obj_path = f"{out_dir}/pottery_fragment_{frag_idx + 1:0{num_digits}d}.obj"
        mtl_path = f"{out_dir}/pottery_fragment_{frag_idx + 1:0{num_digits}d}.mtl"
        write_obj_fragment(obj_path, mtl_path, mtl_name, local_points, local_normals,
                            local_faces, color)
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
