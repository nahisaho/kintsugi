#include "fragment_matching.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <optional>

#include <Eigen/Geometry>
#include <pcl/common/centroid.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/registration/icp.h>
#include <pcl/search/kdtree.h>

namespace kintsugi::core {

namespace {

// 信頼度算出の正規化スケール。破断面の外接直径に対するICP後RMS残差の比率が
// このスケールに達すると信頼度寄与が0になる（値が大きいほど寛容）。
constexpr double kNormalizedFitnessScale = 0.05;
// RGB色空間における最大ユークリッド距離（sqrt(255^2*3)）。
constexpr double kMaxColorDistance = 441.672943;
// 総合信頼度における形状スコアの重み（ADR-0003: 形状適合度を主要項とする）。
constexpr double kShapeWeight = 0.7;
constexpr double kColorWeight = 0.3;
// 破断面抽出（局所曲率推定）の近傍点数。
constexpr int kCurvatureNeighbors = 8;
// 破断面として残す点数の下限。抽出結果がこれ未満の場合はICP退化を避けるため
// 全点を用いる（フォールバック）。
constexpr std::size_t kMinFractureSurfacePoints = 4;

pcl::PointCloud<pcl::PointXYZ>::Ptr toPclCloud(const FragmentMesh& mesh) {
  auto cloud = pcl::PointCloud<pcl::PointXYZ>::Ptr(new pcl::PointCloud<pcl::PointXYZ>());
  cloud->reserve(mesh.vertices.size());
  for (const auto& v : mesh.vertices) {
    cloud->push_back(pcl::PointXYZ(static_cast<float>(v.x), static_cast<float>(v.y),
                                    static_cast<float>(v.z)));
  }
  return cloud;
}

// 各点の局所曲率（表面変動量）を、k近傍点群のPCA共分散行列の最小固有値比
// （λmin / (λ0+λ1+λ2)）として算出する。平滑な原表面（釉薬面等）では0に近く、
// 破断面特有の不規則な凹凸では値が大きくなる（ADR-0003「破断面抽出」）。
std::vector<double> computePointCurvatures(const pcl::PointCloud<pcl::PointXYZ>& cloud) {
  const std::size_t n = cloud.size();
  std::vector<double> curvatures(n, 0.0);
  if (n < 3) {
    return curvatures;
  }

  pcl::search::KdTree<pcl::PointXYZ> tree;
  tree.setInputCloud(pcl::PointCloud<pcl::PointXYZ>::ConstPtr(&cloud, [](const void*) {}));

  std::vector<int> indices;
  std::vector<float> squaredDistances;
  const int k = std::min<int>(kCurvatureNeighbors, static_cast<int>(n));
  for (std::size_t i = 0; i < n; ++i) {
    tree.nearestKSearch(static_cast<int>(i), k, indices, squaredDistances);
    if (indices.size() < 3) {
      continue;
    }
    Eigen::Vector3d mean = Eigen::Vector3d::Zero();
    for (int idx : indices) {
      mean += Eigen::Vector3d(cloud[idx].x, cloud[idx].y, cloud[idx].z);
    }
    mean /= static_cast<double>(indices.size());

    Eigen::Matrix3d covariance = Eigen::Matrix3d::Zero();
    for (int idx : indices) {
      const Eigen::Vector3d diff(cloud[idx].x - mean.x(), cloud[idx].y - mean.y(),
                                  cloud[idx].z - mean.z());
      covariance += diff * diff.transpose();
    }
    covariance /= static_cast<double>(indices.size());

    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(covariance, Eigen::EigenvaluesOnly);
    const Eigen::Vector3d eigenvalues = solver.eigenvalues();  // 昇順。
    const double sum = eigenvalues.sum();
    curvatures[i] = sum > 1e-12 ? std::max(eigenvalues(0), 0.0) / sum : 0.0;
  }
  return curvatures;
}

// 局所曲率が統計的に異常な（平均＋標準偏差を超える）点を破断面点として抽出する
// （ADR-0003: 法線・曲率の不連続領域に基づく破断面抽出）。抽出数が不十分な場合は
// ICPの退化を避けるため全点のインデックスを返す。
std::vector<std::size_t> extractFractureSurfaceIndices(const pcl::PointCloud<pcl::PointXYZ>& cloud) {
  const std::size_t n = cloud.size();
  std::vector<std::size_t> allIndices(n);
  std::iota(allIndices.begin(), allIndices.end(), 0);
  if (n < kMinFractureSurfacePoints) {
    return allIndices;
  }

  const std::vector<double> curvature = computePointCurvatures(cloud);
  const double mean = std::accumulate(curvature.begin(), curvature.end(), 0.0) /
                       static_cast<double>(curvature.size());
  double variance = 0.0;
  for (double c : curvature) {
    variance += (c - mean) * (c - mean);
  }
  variance /= static_cast<double>(curvature.size());
  const double stddev = std::sqrt(variance);
  const double threshold = mean + stddev;

  std::vector<std::size_t> fractureIndices;
  for (std::size_t i = 0; i < n; ++i) {
    if (curvature[i] > threshold) {
      fractureIndices.push_back(i);
    }
  }

  if (fractureIndices.size() < kMinFractureSurfacePoints) {
    return allIndices;  // 抽出が退化する場合は全点にフォールバックする。
  }
  return fractureIndices;
}

pcl::PointCloud<pcl::PointXYZ>::Ptr selectSubset(const pcl::PointCloud<pcl::PointXYZ>& cloud,
                                                  const std::vector<std::size_t>& indices) {
  auto subset = pcl::PointCloud<pcl::PointXYZ>::Ptr(new pcl::PointCloud<pcl::PointXYZ>());
  subset->reserve(indices.size());
  for (std::size_t index : indices) {
    subset->push_back(cloud[index]);
  }
  return subset;
}

// 点群の外接直径（バウンディングボックス対角線長）を算出する。フィッティング残差の
// スケール正規化に用いる。
double boundingDiagonal(const pcl::PointCloud<pcl::PointXYZ>& cloud) {
  if (cloud.empty()) return 0.0;
  pcl::PointXYZ minPt = cloud.front();
  pcl::PointXYZ maxPt = cloud.front();
  for (const auto& p : cloud.points) {
    minPt.x = std::min(minPt.x, p.x);
    minPt.y = std::min(minPt.y, p.y);
    minPt.z = std::min(minPt.z, p.z);
    maxPt.x = std::max(maxPt.x, p.x);
    maxPt.y = std::max(maxPt.y, p.y);
    maxPt.z = std::max(maxPt.z, p.z);
  }
  const double dx = maxPt.x - minPt.x;
  const double dy = maxPt.y - minPt.y;
  const double dz = maxPt.z - minPt.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

struct IcpOutcome {
  double shapeScore = 0.0;  // 0〜100
  bool converged = false;
  PropagatedPose pose;
};

// ADR-0003ステップ2: 破断面点群のみを用いたICPによる姿勢精密化。重心を揃えた
// 初期姿勢から開始し、収束状態を確認したうえで、収束後のフィッティング残差を
// 破片スケールで正規化して形状一致度スコアを算出する。ICPが収束しなかった場合は
// 残差の値によらず信頼度0とする（Issue #4: 収束判定の未反映を是正）。
IcpOutcome runIcp(const pcl::PointCloud<pcl::PointXYZ>::Ptr& source,
                   const pcl::PointCloud<pcl::PointXYZ>::Ptr& target) {
  Eigen::Vector4f sourceCentroid;
  Eigen::Vector4f targetCentroid;
  pcl::compute3DCentroid(*source, sourceCentroid);
  pcl::compute3DCentroid(*target, targetCentroid);
  Eigen::Matrix4f initialGuess = Eigen::Matrix4f::Identity();
  initialGuess.block<3, 1>(0, 3) = (targetCentroid - sourceCentroid).head<3>();

  pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
  icp.setInputSource(source);
  icp.setInputTarget(target);
  icp.setMaxCorrespondenceDistance(1000.0);
  icp.setMaximumIterations(200);
  icp.setTransformationEpsilon(1e-10);
  icp.setEuclideanFitnessEpsilon(1e-10);

  pcl::PointCloud<pcl::PointXYZ> aligned;
  icp.align(aligned, initialGuess);

  IcpOutcome outcome;
  outcome.converged = icp.hasConverged();

  const double rms = std::sqrt(icp.getFitnessScore());
  const double scale =
      0.5 * (boundingDiagonal(*source) + boundingDiagonal(*target));
  const double normalizedFitness =
      scale > 0.0 ? std::min(rms / scale / kNormalizedFitnessScale, 1.0) : 1.0;

  outcome.shapeScore =
      outcome.converged ? std::clamp((1.0 - normalizedFitness) * 100.0, 0.0, 100.0) : 0.0;

  const Eigen::Matrix4f transform = icp.getFinalTransformation();
  outcome.pose.translationMm.x = transform(0, 3);
  outcome.pose.translationMm.y = transform(1, 3);
  outcome.pose.translationMm.z = transform(2, 3);
  const Eigen::Matrix3f rotationMatrix = transform.block<3, 3>(0, 0);
  const Eigen::Quaternionf quaternion(rotationMatrix);
  outcome.pose.rotation.w = quaternion.w();
  outcome.pose.rotation.x = quaternion.x();
  outcome.pose.rotation.y = quaternion.y();
  outcome.pose.rotation.z = quaternion.z();

  return outcome;
}

// ADR-0003ステップ3: 破断面近傍の頂点色平均同士のユークリッド距離から
// 色・模様連続性スコア（0〜100）を算出する（REQ-POTTERY-005）。全体平均ではなく
// 破断面点群のインデックスに対応する色のみを用いることで、境界近傍の局所的な
// 色連続性を評価する（Issue #4: 全体平均RGB比較からの是正）。双方に色情報が
// ある場合のみ計算する。
std::optional<double> computeColorScore(const FragmentMesh& a, const std::vector<std::size_t>& indicesA,
                                         const FragmentMesh& b, const std::vector<std::size_t>& indicesB) {
  if (a.colors.empty() || b.colors.empty()) {
    return std::nullopt;
  }

  const auto meanColor = [](const std::vector<ColorRGB>& colors, const std::vector<std::size_t>& indices) {
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    std::size_t count = 0;
    for (std::size_t index : indices) {
      if (index >= colors.size()) continue;
      sumR += colors[index].r;
      sumG += colors[index].g;
      sumB += colors[index].b;
      ++count;
    }
    if (count == 0) {
      return std::array<double, 3>{0.0, 0.0, 0.0};
    }
    const double n = static_cast<double>(count);
    return std::array<double, 3>{sumR / n, sumG / n, sumB / n};
  };

  const auto colorA = meanColor(a.colors, indicesA);
  const auto colorB = meanColor(b.colors, indicesB);
  const double dr = colorA[0] - colorB[0];
  const double dg = colorA[1] - colorB[1];
  const double db = colorA[2] - colorB[2];
  const double distance = std::sqrt(dr * dr + dg * dg + db * db);
  const double normalizedDistance = std::min(distance / kMaxColorDistance, 1.0);
  return std::clamp((1.0 - normalizedDistance) * 100.0, 0.0, 100.0);
}

// 破片1点分の破断面抽出結果（ICP用の点群サブセットと、元のvertices/colors配列に
// 対応するインデックス）。全破片についてO(n)回のみ計算し、ペアごとの再計算を避ける。
struct FragmentAnalysis {
  std::vector<std::size_t> fractureIndices;
  pcl::PointCloud<pcl::PointXYZ>::Ptr fractureCloud;
};

FragmentAnalysis analyzeFragment(const FragmentMesh& mesh) {
  const auto fullCloud = toPclCloud(mesh);
  FragmentAnalysis analysis;
  analysis.fractureIndices = extractFractureSurfaceIndices(*fullCloud);
  analysis.fractureCloud = selectSubset(*fullCloud, analysis.fractureIndices);
  return analysis;
}

}  // namespace

std::vector<JoinCandidate> computeJoinCandidates(const std::vector<FragmentMesh>& fragments) {
  std::vector<JoinCandidate> candidates;
  const std::size_t n = fragments.size();
  candidates.reserve(n * (n - 1) / 2);

  std::vector<FragmentAnalysis> analyses;
  analyses.reserve(n);
  for (const auto& fragment : fragments) {
    analyses.push_back(analyzeFragment(fragment));
  }

  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 1; j < n; ++j) {
      const IcpOutcome icpOutcome = runIcp(analyses[i].fractureCloud, analyses[j].fractureCloud);
      const std::optional<double> colorScore =
          computeColorScore(fragments[i], analyses[i].fractureIndices, fragments[j],
                             analyses[j].fractureIndices);

      JoinCandidate candidate;
      candidate.fragmentIdA = i;
      candidate.fragmentIdB = j;
      candidate.pose = icpOutcome.pose;
      candidate.evidence.shapeScore = icpOutcome.shapeScore;
      candidate.evidence.colorScore = colorScore;
      candidate.evidence.icpConverged = icpOutcome.converged;
      candidate.confidenceScore =
          colorScore.has_value()
              ? std::clamp(kShapeWeight * icpOutcome.shapeScore + kColorWeight * *colorScore, 0.0,
                           100.0)
              : icpOutcome.shapeScore;
      candidates.push_back(candidate);
    }
  }

  return candidates;
}

}  // namespace kintsugi::core
