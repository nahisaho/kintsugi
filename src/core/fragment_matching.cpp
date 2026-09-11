#include "fragment_matching.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include <Eigen/Geometry>
#include <pcl/common/centroid.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/registration/icp.h>

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

pcl::PointCloud<pcl::PointXYZ>::Ptr toPclCloud(const FragmentMesh& mesh) {
  auto cloud = pcl::PointCloud<pcl::PointXYZ>::Ptr(new pcl::PointCloud<pcl::PointXYZ>());
  cloud->reserve(mesh.vertices.size());
  for (const auto& v : mesh.vertices) {
    cloud->push_back(pcl::PointXYZ(static_cast<float>(v.x), static_cast<float>(v.y),
                                    static_cast<float>(v.z)));
  }
  return cloud;
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
  PropagatedPose pose;
};

// ADR-0003ステップ2: ICPによる姿勢精密化。重心を揃えた初期姿勢から開始し、
// 収束後のフィッティング残差を破片スケールで正規化して形状一致度スコアを算出する。
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

  const double rms = std::sqrt(icp.getFitnessScore());
  const double scale =
      0.5 * (boundingDiagonal(*source) + boundingDiagonal(*target));
  const double normalizedFitness =
      scale > 0.0 ? std::min(rms / scale / kNormalizedFitnessScale, 1.0) : 1.0;

  IcpOutcome outcome;
  outcome.shapeScore = std::clamp((1.0 - normalizedFitness) * 100.0, 0.0, 100.0);

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

// ADR-0003ステップ3: 頂点色平均同士のユークリッド距離から色・模様連続性スコア
// （0〜100）を算出する（REQ-POTTERY-005）。双方に色情報がある場合のみ計算する。
std::optional<double> computeColorScore(const FragmentMesh& a, const FragmentMesh& b) {
  if (a.colors.empty() || b.colors.empty()) {
    return std::nullopt;
  }

  const auto meanColor = [](const std::vector<ColorRGB>& colors) {
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    for (const auto& c : colors) {
      sumR += c.r;
      sumG += c.g;
      sumB += c.b;
    }
    const double n = static_cast<double>(colors.size());
    return std::array<double, 3>{sumR / n, sumG / n, sumB / n};
  };

  const auto colorA = meanColor(a.colors);
  const auto colorB = meanColor(b.colors);
  const double dr = colorA[0] - colorB[0];
  const double dg = colorA[1] - colorB[1];
  const double db = colorA[2] - colorB[2];
  const double distance = std::sqrt(dr * dr + dg * dg + db * db);
  const double normalizedDistance = std::min(distance / kMaxColorDistance, 1.0);
  return std::clamp((1.0 - normalizedDistance) * 100.0, 0.0, 100.0);
}

}  // namespace

std::vector<JoinCandidate> computeJoinCandidates(const std::vector<FragmentMesh>& fragments) {
  std::vector<JoinCandidate> candidates;
  const std::size_t n = fragments.size();
  candidates.reserve(n * (n - 1) / 2);

  for (std::size_t i = 0; i < n; ++i) {
    const auto cloudA = toPclCloud(fragments[i]);
    for (std::size_t j = i + 1; j < n; ++j) {
      const auto cloudB = toPclCloud(fragments[j]);
      const IcpOutcome icpOutcome = runIcp(cloudA, cloudB);
      const std::optional<double> colorScore = computeColorScore(fragments[i], fragments[j]);

      JoinCandidate candidate;
      candidate.fragmentIdA = i;
      candidate.fragmentIdB = j;
      candidate.pose = icpOutcome.pose;
      candidate.evidence.shapeScore = icpOutcome.shapeScore;
      candidate.evidence.colorScore = colorScore;
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
