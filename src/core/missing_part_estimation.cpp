#include "missing_part_estimation.hpp"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <limits>

namespace kintsugi::core {

namespace {

constexpr int kHeightBins = 10;
constexpr int kAngleBins = 24;
constexpr double kSymmetryRelativeStdDevThreshold = 0.12;
constexpr double kMinQualifyingBinRatio = 0.8;
constexpr int kMinPointsPerHeightBin = 3;
constexpr int kMinQualifyingHeightBins = 2;

// PropagatedPoseの回転（単位四元数）でベクトルvを回転する。
Eigen::Vector3d rotateVector(const Quaternion& q, const Eigen::Vector3d& v) {
  const Eigen::Vector3d qv(q.x, q.y, q.z);
  const Eigen::Vector3d t = 2.0 * qv.cross(v);
  return v + q.w * t + qv.cross(t);
}

Eigen::Vector3d transformVertex(const PropagatedPose& pose, const Vec3& vertex) {
  const Eigen::Vector3d v(vertex.x, vertex.y, vertex.z);
  const Eigen::Vector3d rotated = rotateVector(pose.rotation, v);
  return rotated + Eigen::Vector3d(pose.translationMm.x, pose.translationMm.y, pose.translationMm.z);
}

struct HeightBinStats {
  int count = 0;
  double radiusSum = 0.0;
  double radiusSumSq = 0.0;
  std::vector<bool> angleOccupied = std::vector<bool>(kAngleBins, false);

  double mean() const { return count > 0 ? radiusSum / count : 0.0; }
  double stddev() const {
    if (count < 2) return 0.0;
    const double m = mean();
    const double variance = std::max(0.0, radiusSumSq / count - m * m);
    return std::sqrt(variance);
  }
};

// 候補となる回転軸1件について、その軸に垂直な断面が円形（半径がほぼ一定）で
// あるかどうかを、高さビンごとの半径相対標準偏差（円形断面フィット残差）で
// 評価した結果。ADR-0005が要求する「円柱/円形断面フィットによる回転軸推定」を
// 実現するため、PCAの最大分散方向を無条件に採用するのではなく、複数の候補軸に
// ついてこの残差を比較し、最も円形断面フィットが良い軸を採用する。
struct AxisCandidateEvaluation {
  Eigen::Vector3d axis;
  Eigen::Vector3d u;
  Eigen::Vector3d v;
  std::vector<double> heights;
  std::vector<double> radii;
  std::vector<double> angles;
  double minHeight = 0.0;
  double maxHeight = 0.0;
  std::vector<HeightBinStats> bins;
  int qualifyingBinCount = 0;
  double qualifyingRatio = 0.0;
  // 円形断面フィット残差（データが十分な高さビンにおける半径相対標準偏差の平均）。
  // 値が小さいほど、その軸に垂直な断面がより真円に近いことを示す。
  double meanCircularFitResidual = std::numeric_limits<double>::max();
  bool valid = false;
};

// 与えられた候補軸axisについて、頂点群を円柱座標(高さ・半径・角度)へ変換し、
// 高さビンごとの円形断面フィット残差を評価する。
AxisCandidateEvaluation evaluateAxisCandidate(const std::vector<Vec3>& vertices,
                                               const Eigen::Vector3d& centroid,
                                               const Eigen::Vector3d& axis) {
  AxisCandidateEvaluation eval;
  eval.axis = axis;

  Eigen::Vector3d seed =
      (std::fabs(axis.x()) < 0.9) ? Eigen::Vector3d(1.0, 0.0, 0.0) : Eigen::Vector3d(0.0, 1.0, 0.0);
  eval.u = (seed - seed.dot(axis) * axis).normalized();
  eval.v = axis.cross(eval.u).normalized();

  eval.heights.resize(vertices.size());
  eval.radii.resize(vertices.size());
  eval.angles.resize(vertices.size());
  eval.minHeight = std::numeric_limits<double>::max();
  eval.maxHeight = std::numeric_limits<double>::lowest();
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const Eigen::Vector3d d = Eigen::Vector3d(vertices[i].x, vertices[i].y, vertices[i].z) - centroid;
    const double h = d.dot(axis);
    const Eigen::Vector3d perp = d - h * axis;
    const double r = perp.norm();
    double theta = std::atan2(perp.dot(eval.v), perp.dot(eval.u));
    if (theta < 0.0) {
      theta += 2.0 * M_PI;
    }
    eval.heights[i] = h;
    eval.radii[i] = r;
    eval.angles[i] = theta;
    eval.minHeight = std::min(eval.minHeight, h);
    eval.maxHeight = std::max(eval.maxHeight, h);
  }

  const double heightRange = eval.maxHeight - eval.minHeight;
  if (heightRange <= 1e-6) {
    return eval;  // 軸方向の広がりがなく評価不能（invalidのまま）。
  }
  const double heightBinWidth = heightRange / kHeightBins;
  const double angleBinWidth = 2.0 * M_PI / kAngleBins;

  eval.bins.assign(kHeightBins, HeightBinStats{});
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    int heightBinIndex = static_cast<int>((eval.heights[i] - eval.minHeight) / heightBinWidth);
    heightBinIndex = std::clamp(heightBinIndex, 0, kHeightBins - 1);
    int angleBinIndex = static_cast<int>(eval.angles[i] / angleBinWidth);
    angleBinIndex = std::clamp(angleBinIndex, 0, kAngleBins - 1);

    HeightBinStats& bin = eval.bins[heightBinIndex];
    bin.count += 1;
    bin.radiusSum += eval.radii[i];
    bin.radiusSumSq += eval.radii[i] * eval.radii[i];
    bin.angleOccupied[angleBinIndex] = true;
  }

  int qualifyingBinCount = 0;
  int lowVarianceBinCount = 0;
  double residualSum = 0.0;
  for (const HeightBinStats& bin : eval.bins) {
    if (bin.count < kMinPointsPerHeightBin) {
      continue;
    }
    qualifyingBinCount += 1;
    const double mean = bin.mean();
    const double relativeStdDev = (mean > 1e-9) ? bin.stddev() / mean : 0.0;
    residualSum += relativeStdDev;
    if (relativeStdDev <= kSymmetryRelativeStdDevThreshold) {
      lowVarianceBinCount += 1;
    }
  }
  eval.qualifyingBinCount = qualifyingBinCount;
  if (qualifyingBinCount >= kMinQualifyingHeightBins) {
    eval.qualifyingRatio = static_cast<double>(lowVarianceBinCount) / static_cast<double>(qualifyingBinCount);
    eval.meanCircularFitResidual = residualSum / static_cast<double>(qualifyingBinCount);
    eval.valid = true;
  }
  return eval;
}

}  // namespace

std::vector<Vec3> collectAssembledVertices(const AssemblyState& assemblyState,
                                            const std::vector<FragmentMesh>& fragments) {
  std::vector<Vec3> collected;
  for (const std::size_t fragmentId : assemblyState.fragmentIds) {
    if (fragmentId >= fragments.size()) {
      continue;
    }
    const auto pose = assemblyState.resolvedPose(fragmentId);
    if (!pose.has_value()) {
      continue;  // 未接合の破片は姿勢が未確定のため対象外。
    }
    for (const Vec3& vertex : fragments[fragmentId].vertices) {
      const Eigen::Vector3d transformed = transformVertex(*pose, vertex);
      collected.push_back(Vec3{transformed.x(), transformed.y(), transformed.z()});
    }
  }
  return collected;
}

std::optional<std::vector<EstimatedShapeRecord>> estimateMissingParts(
    const AssemblyState& assemblyState, const std::vector<FragmentMesh>& fragments) {
  const std::vector<Vec3> vertices = collectAssembledVertices(assemblyState, fragments);
  if (vertices.size() < static_cast<std::size_t>(kMinPointsPerHeightBin * kMinQualifyingHeightBins)) {
    return std::nullopt;  // データ不足のため対称性を判定できない。
  }

  // 重心を求める。
  Eigen::Vector3d centroid = Eigen::Vector3d::Zero();
  for (const Vec3& v : vertices) {
    centroid += Eigen::Vector3d(v.x, v.y, v.z);
  }
  centroid /= static_cast<double>(vertices.size());

  // 共分散行列の3つの固有ベクトル（互いに直交）を回転軸候補とする。
  // ADR-0005は円柱/円形断面フィットによる軸推定を要求するため、PCA最大分散方向を
  // 無条件に採用せず、各候補について円形断面フィット残差(evaluateAxisCandidate)を
  // 計算し、残差が最小（＝断面が最も真円に近い）候補を採用する。これにより
  // 「短くて太い」（高さ方向の分散が半径方向より小さい）形状でも、最大分散方向
  // ではなく実際の回転軸を正しく選択できる。
  Eigen::Matrix3d covariance = Eigen::Matrix3d::Zero();
  for (const Vec3& v : vertices) {
    const Eigen::Vector3d d = Eigen::Vector3d(v.x, v.y, v.z) - centroid;
    covariance += d * d.transpose();
  }
  covariance /= static_cast<double>(vertices.size());

  const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(covariance);

  std::optional<AxisCandidateEvaluation> best;
  for (int col = 0; col < 3; ++col) {
    const Eigen::Vector3d candidateAxis = solver.eigenvectors().col(col).normalized();
    AxisCandidateEvaluation eval = evaluateAxisCandidate(vertices, centroid, candidateAxis);
    if (!eval.valid || eval.qualifyingRatio < kMinQualifyingBinRatio) {
      continue;
    }
    // 選定基準: まず円形断面と判定された高さビンの割合(qualifyingRatio)が高い
    // 軸を優先し（対象全体で円形断面が最も安定して成立する軸を選ぶ）、同率の
    // 場合のみ円形断面フィット残差(meanCircularFitResidual)が小さい方を選ぶ。
    // 残差のみで比較すると、判定対象ビン数が少ない軸が偶然低残差になり誤選択
    // される場合があるため、この二段階比較で真の回転軸を安定して選ぶ。
    if (!best.has_value() || eval.qualifyingRatio > best->qualifyingRatio ||
        (eval.qualifyingRatio == best->qualifyingRatio &&
         eval.meanCircularFitResidual < best->meanCircularFitResidual)) {
      best = std::move(eval);
    }
  }
  if (!best.has_value()) {
    return std::nullopt;  // どの候補軸でも回転対称性が検出できない。
  }

  const Eigen::Vector3d axis = best->axis;
  const Eigen::Vector3d u = best->u;
  const Eigen::Vector3d v = best->v;
  const double minHeight = best->minHeight;
  const double heightRange = best->maxHeight - best->minHeight;
  const double heightBinWidth = heightRange / kHeightBins;
  const double angleBinWidth = 2.0 * M_PI / kAngleBins;
  std::vector<HeightBinStats>& bins = best->bins;

  // 高さビンごとのプロファイル半径（データがないビンは前後の最近傍で補間）。
  std::vector<double> profileRadius(kHeightBins, 0.0);
  std::vector<bool> hasProfile(kHeightBins, false);
  for (int i = 0; i < kHeightBins; ++i) {
    if (bins[i].count > 0) {
      profileRadius[i] = bins[i].mean();
      hasProfile[i] = true;
    }
  }
  for (int i = 0; i < kHeightBins; ++i) {
    if (hasProfile[i]) continue;
    int before = -1;
    for (int j = i - 1; j >= 0; --j) {
      if (hasProfile[j]) { before = j; break; }
    }
    int after = -1;
    for (int j = i + 1; j < kHeightBins; ++j) {
      if (hasProfile[j]) { after = j; break; }
    }
    if (before >= 0 && after >= 0) {
      const double t = static_cast<double>(i - before) / static_cast<double>(after - before);
      profileRadius[i] = profileRadius[before] * (1.0 - t) + profileRadius[after] * t;
    } else if (before >= 0) {
      profileRadius[i] = profileRadius[before];
    } else if (after >= 0) {
      profileRadius[i] = profileRadius[after];
    }
  }

  // 欠損している(高さビン, 角度ビン)の格子を検出し、パッチ面を生成する。
  EstimatedShapeRecord patch;
  for (int hBin = 0; hBin < kHeightBins; ++hBin) {
    if (bins[hBin].count == 0) {
      continue;  // 高さ方向に実測データがない区間は外挿しない。
    }
    const double hLo = minHeight + hBin * heightBinWidth;
    const double hHi = minHeight + (hBin + 1) * heightBinWidth;
    const double radius = profileRadius[hBin];
    for (int aBin = 0; aBin < kAngleBins; ++aBin) {
      if (bins[hBin].angleOccupied[aBin]) {
        continue;
      }
      const double aLo = aBin * angleBinWidth;
      const double aHi = (aBin + 1) * angleBinWidth;

      auto makePoint = [&](double h, double angle) -> Vec3 {
        const Eigen::Vector3d p =
            centroid + axis * h + radius * (std::cos(angle) * u + std::sin(angle) * v);
        return Vec3{p.x(), p.y(), p.z()};
      };

      const std::size_t base = patch.vertices.size();
      patch.vertices.push_back(makePoint(hLo, aLo));
      patch.vertices.push_back(makePoint(hLo, aHi));
      patch.vertices.push_back(makePoint(hHi, aHi));
      patch.vertices.push_back(makePoint(hHi, aLo));
      patch.faces.push_back(Triangle{base + 0, base + 1, base + 2});
      patch.faces.push_back(Triangle{base + 0, base + 2, base + 3});
    }
  }

  std::vector<EstimatedShapeRecord> result;
  if (!patch.vertices.empty()) {
    result.push_back(std::move(patch));
  }
  return result;
}

}  // namespace kintsugi::core
