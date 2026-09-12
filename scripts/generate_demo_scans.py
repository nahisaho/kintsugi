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


def generate_decoy_fragment(rng: np.random.Generator, patch_size: int = 10):
    """壺本体とは接合しない、別由来(別の器・がれき)を模した破片を生成する。

    壺のプロファイルとは無関係な局所曲率(放物面近似、曲率半径をランダムに
    大きく変える)を持つ小片を、壺本体から離れた位置・向きに配置する。
    復元シミュレーションにおいて「接合先が存在しない破片」を混在させる
    ことで、クラスタリング・接合候補提示が全破片を無理に接合しようと
    しないことを確認できるデータになる。
    """
    span = rng.uniform(10.0, 22.0)
    u = np.linspace(-span, span, patch_size)
    v = np.linspace(-span, span, patch_size)
    grid_u, grid_v = np.meshgrid(u, v)

    # 曲率半径は壺本体(半径50mm程度)と明確に異なる値(ほぼ平坦〜強い湾曲まで)
    # をランダムに選び、壺表面のどの部分ともフィットしない形状にする。
    curvature_radius = rng.uniform(15.0, 300.0) * rng.choice([-1.0, 1.0])
    grid_z = (grid_u ** 2 + grid_v ** 2) / (2.0 * curvature_radius)
    grid_z += rng.normal(0, 0.3, size=grid_z.shape)

    local_points = np.stack([grid_u.ravel(), grid_v.ravel(), grid_z.ravel()], axis=1)

    dzdu = grid_u / curvature_radius
    dzdv = grid_v / curvature_radius
    local_normals = np.stack(
        [-dzdu.ravel(), -dzdv.ravel(), np.ones(grid_u.size)], axis=1)
    local_normals /= np.linalg.norm(local_normals, axis=1, keepdims=True)

    # ランダムな回転(向き)を適用し、壺本体の局所法線方向と揃わないようにする。
    axis = rng.normal(0, 1, size=3)
    axis /= np.linalg.norm(axis)
    angle = rng.uniform(0, 2 * np.pi)
    cos_a, sin_a = np.cos(angle), np.sin(angle)
    kx, ky, kz = axis
    k_mat = np.array([[0, -kz, ky], [kz, 0, -kx], [-ky, kx, 0]])
    rotation = np.eye(3) + sin_a * k_mat + (1 - cos_a) * (k_mat @ k_mat)
    local_points = local_points @ rotation.T
    local_normals = local_normals @ rotation.T

    # 壺本体(高さ0〜120mm, 半径〜90mm程度)から明確に離れた位置に配置する。
    theta = rng.uniform(0, 2 * np.pi)
    distance = rng.uniform(160.0, 260.0)
    center = np.array([
        distance * np.cos(theta),
        distance * np.sin(theta),
        rng.uniform(-40.0, 160.0),
    ])
    local_points += center

    faces = []
    for i in range(patch_size - 1):
        for j in range(patch_size - 1):
            v00 = i * patch_size + j
            v01 = i * patch_size + (j + 1)
            v10 = (i + 1) * patch_size + j
            v11 = (i + 1) * patch_size + (j + 1)
            faces.append((v00, v10, v11))
            faces.append((v00, v11, v01))

    return local_points, local_normals, faces


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
             points_per_fragment_target: int = 250,
             missing_ratio: float = 0.15,
             num_unrelated: int = 5) -> None:
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

    # 実際の出土状況を模し、一部の破片を欠損(紛失)として扱い出力しない。
    # 復元シミュレーションでは全破片が揃っているとは限らないため、
    # 一定割合をランダムに間引くことで、破片が足りない状態での接合・
    # 復元処理を検証できるようにする。
    num_missing = int(round(num_fragments * missing_ratio))
    missing_indices: set[int] = set()
    if num_missing > 0:
        missing_indices = set(
            rng.choice(num_fragments, size=num_missing, replace=False).tolist())

    written = 0
    skipped_missing = 0
    num_digits = len(str(num_fragments + num_unrelated))
    for frag_idx in range(num_fragments):
        if frag_idx in missing_indices:
            skipped_missing += 1
            continue
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

    # 壺本体とは接合しない「マッチしない破片」(別の器・がれき由来を想定)を
    # 追加する。番号は壺本体破片の続き(num_fragments+1〜)を割り当てる。
    decoy_palette = [
        (110, 110, 120), (130, 120, 140), (100, 105, 100), (140, 135, 130),
    ]
    for decoy_i in range(num_unrelated):
        frag_number = num_fragments + decoy_i + 1
        local_points, local_normals, local_faces = generate_decoy_fragment(rng)
        color = decoy_palette[decoy_i % len(decoy_palette)]
        mtl_name = f"fragment{frag_number}"
        obj_path = f"{out_dir}/pottery_fragment_{frag_number:0{num_digits}d}.obj"
        mtl_path = f"{out_dir}/pottery_fragment_{frag_number:0{num_digits}d}.mtl"
        write_obj_fragment(obj_path, mtl_path, mtl_name, local_points, local_normals,
                            local_faces, color)
        written += 1

    print(f"wrote {written} fragment files (requested {num_fragments}, "
          f"{skipped_missing} treated as missing/lost, "
          f"{num_unrelated} unrelated/non-matching decoys added) "
          f"from {len(grid_a)} surface points into {out_dir}/")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--num-fragments", type=int, default=100,
                         help="生成する破片数(デフォルト: 100)")
    parser.add_argument("--out-dir", default="demo_data",
                         help="出力先ディレクトリ(デフォルト: demo_data)")
    parser.add_argument("--seed", type=int, default=42, help="乱数シード")
    parser.add_argument("--missing-ratio", type=float, default=0.15,
                         help="欠損(紛失)扱いにして出力しない破片の割合"
                              "(デフォルト: 0.15 = 約15%)")
    parser.add_argument("--unrelated-count", type=int, default=5,
                         help="壺本体と接合しない破片(別由来・がれき)の"
                              "追加数(デフォルト: 5)")
    args = parser.parse_args()
    generate(args.num_fragments, args.out_dir, args.seed,
              missing_ratio=args.missing_ratio, num_unrelated=args.unrelated_count)


if __name__ == "__main__":
    main()
