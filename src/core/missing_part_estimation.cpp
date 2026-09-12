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

  // 共分散行列の最大固有値に対応する固有ベクトルを回転軸候補とする（PCA）。
  Eigen::Matrix3d covariance = Eigen::Matrix3d::Zero();
  for (const Vec3& v : vertices) {
    const Eigen::Vector3d d = Eigen::Vector3d(v.x, v.y, v.z) - centroid;
    covariance += d * d.transpose();
  }
  covariance /= static_cast<double>(vertices.size());

  const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(covariance);
  const Eigen::Vector3d axis = solver.eigenvectors().col(2).normalized();  // 最大固有値は末尾列。

  // 軸に垂直な正規直交基底(u, v)をGram-Schmidtで構成する。
  Eigen::Vector3d seed = (std::fabs(axis.x()) < 0.9) ? Eigen::Vector3d(1.0, 0.0, 0.0) : Eigen::Vector3d(0.0, 1.0, 0.0);
  Eigen::Vector3d u = (seed - seed.dot(axis) * axis).normalized();
  Eigen::Vector3d v = axis.cross(u).normalized();

  // 各頂点を円柱座標(高さh, 半径r, 角度theta)へ変換する。
  std::vector<double> heights(vertices.size());
  std::vector<double> radii(vertices.size());
  std::vector<double> angles(vertices.size());
  double minHeight = std::numeric_limits<double>::max();
  double maxHeight = std::numeric_limits<double>::lowest();
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const Eigen::Vector3d d = Eigen::Vector3d(vertices[i].x, vertices[i].y, vertices[i].z) - centroid;
    const double h = d.dot(axis);
    const Eigen::Vector3d perp = d - h * axis;
    const double r = perp.norm();
    double theta = std::atan2(perp.dot(v), perp.dot(u));
    if (theta < 0.0) {
      theta += 2.0 * M_PI;
    }
    heights[i] = h;
    radii[i] = r;
    angles[i] = theta;
    minHeight = std::min(minHeight, h);
    maxHeight = std::max(maxHeight, h);
  }

  const double heightRange = maxHeight - minHeight;
  if (heightRange <= 1e-6) {
    return std::nullopt;  // 高さ方向の広がりがなく軸方向の判定ができない。
  }
  const double heightBinWidth = heightRange / kHeightBins;
  const double angleBinWidth = 2.0 * M_PI / kAngleBins;

  std::vector<HeightBinStats> bins(kHeightBins);
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    int heightBinIndex = static_cast<int>((heights[i] - minHeight) / heightBinWidth);
    heightBinIndex = std::clamp(heightBinIndex, 0, kHeightBins - 1);
    int angleBinIndex = static_cast<int>(angles[i] / angleBinWidth);
    angleBinIndex = std::clamp(angleBinIndex, 0, kAngleBins - 1);

    HeightBinStats& bin = bins[heightBinIndex];
    bin.count += 1;
    bin.radiusSum += radii[i];
    bin.radiusSumSq += radii[i] * radii[i];
    bin.angleOccupied[angleBinIndex] = true;
  }

  // 回転対称性判定: データが十分にある高さビンのうち、半径の相対標準偏差が
  // 閾値以下であるものの割合が基準を満たさなければ推定を行わない。
  int qualifyingBinCount = 0;
  int lowVarianceBinCount = 0;
  for (const HeightBinStats& bin : bins) {
    if (bin.count < kMinPointsPerHeightBin) {
      continue;
    }
    qualifyingBinCount += 1;
    const double mean = bin.mean();
    const double relativeStdDev = (mean > 1e-9) ? bin.stddev() / mean : 0.0;
    if (relativeStdDev <= kSymmetryRelativeStdDevThreshold) {
      lowVarianceBinCount += 1;
    }
  }
  if (qualifyingBinCount < kMinQualifyingHeightBins) {
    return std::nullopt;  // 判定に十分なデータがない。
  }
  const double qualifyingRatio = static_cast<double>(lowVarianceBinCount) / static_cast<double>(qualifyingBinCount);
  if (qualifyingRatio < kMinQualifyingBinRatio) {
    return std::nullopt;  // 回転対称性が検出できない。
  }

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
