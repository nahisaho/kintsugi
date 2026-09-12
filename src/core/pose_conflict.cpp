#include "pose_conflict.hpp"

#include <algorithm>
#include <cmath>

#include "fragment_matching.hpp"

namespace kintsugi::core {

namespace {
constexpr double kTranslationConflictThresholdMm = 1.0;
constexpr double kRotationConflictThresholdDegrees = 2.0;
constexpr double kPi = 3.14159265358979323846;
}  // namespace

PropagatedPose composePoses(const PropagatedPose& outer, const PropagatedPose& inner) {
  // 四元数積: outer.rotation * inner.rotation
  const Quaternion& a = outer.rotation;
  const Quaternion& b = inner.rotation;
  Quaternion rotation;
  rotation.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
  rotation.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
  rotation.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
  rotation.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;

  // outer.rotationでinner.translationMmを回転してからouter.translationMmを加算する。
  const Vec3& v = inner.translationMm;
  const double qw = a.w;
  const double qx = a.x;
  const double qy = a.y;
  const double qz = a.z;
  // 標準的なベクトル回転公式: v' = v + 2*qvec×(qvec×v + qw*v)
  const double cx = qy * v.z - qz * v.y;
  const double cy = qz * v.x - qx * v.z;
  const double cz = qx * v.y - qy * v.x;
  const double tx = cx + qw * v.x;
  const double ty = cy + qw * v.y;
  const double tz = cz + qw * v.z;
  const double rx = qy * tz - qz * ty;
  const double ry = qz * tx - qx * tz;
  const double rz = qx * ty - qy * tx;
  const double rotatedX = v.x + 2.0 * rx;
  const double rotatedY = v.y + 2.0 * ry;
  const double rotatedZ = v.z + 2.0 * rz;

  PropagatedPose result;
  result.rotation = rotation;
  result.translationMm.x = outer.translationMm.x + rotatedX;
  result.translationMm.y = outer.translationMm.y + rotatedY;
  result.translationMm.z = outer.translationMm.z + rotatedZ;
  return result;
}

PropagatedPose invertPose(const PropagatedPose& pose) {
  // 単位四元数の逆回転は共役に等しい。
  PropagatedPose inverseRotationOnly;
  inverseRotationOnly.rotation = Quaternion{pose.rotation.w, -pose.rotation.x, -pose.rotation.y, -pose.rotation.z};
  inverseRotationOnly.translationMm = Vec3{0.0, 0.0, 0.0};

  // t' = -(q^-1 . t) : q^-1で並進を回転してから符号反転する。
  PropagatedPose negatedTranslation;
  negatedTranslation.rotation = Quaternion{1.0, 0.0, 0.0, 0.0};
  negatedTranslation.translationMm = pose.translationMm;
  const PropagatedPose rotatedNegated = composePoses(inverseRotationOnly, negatedTranslation);

  PropagatedPose result;
  result.rotation = inverseRotationOnly.rotation;
  result.translationMm.x = -rotatedNegated.translationMm.x;
  result.translationMm.y = -rotatedNegated.translationMm.y;
  result.translationMm.z = -rotatedNegated.translationMm.z;
  return result;
}

PropagatedPose propagatePose(const JoinCandidate& candidate, const FragmentAnchorMap& anchors) {
  const auto it = anchors.find(candidate.fragmentIdA);
  const PropagatedPose anchorA =
      (it != anchors.end()) ? it->second : PropagatedPose{Vec3{0.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
  return composePoses(anchorA, candidate.pose);
}

double translationDifferenceMm(const PropagatedPose& poseA, const PropagatedPose& poseB) {
  const double dx = poseA.translationMm.x - poseB.translationMm.x;
  const double dy = poseA.translationMm.y - poseB.translationMm.y;
  const double dz = poseA.translationMm.z - poseB.translationMm.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double rotationDifferenceDegrees(const PropagatedPose& poseA, const PropagatedPose& poseB) {
  // 単位四元数q, -qは同一回転を表すため、内積の絶対値を用いて
  // 最短相対回転角を求める（2*acos(|dot(qA, qB)|)）。
  const double dot = poseA.rotation.w * poseB.rotation.w + poseA.rotation.x * poseB.rotation.x +
                      poseA.rotation.y * poseB.rotation.y + poseA.rotation.z * poseB.rotation.z;
  const double clampedDot = std::clamp(std::fabs(dot), 0.0, 1.0);
  const double angleRadians = 2.0 * std::acos(clampedDot);
  return angleRadians * (180.0 / kPi);
}

bool isConflicting(const PropagatedPose& poseA, const PropagatedPose& poseB) {
  return translationDifferenceMm(poseA, poseB) > kTranslationConflictThresholdMm ||
         rotationDifferenceDegrees(poseA, poseB) > kRotationConflictThresholdDegrees;
}

}  // namespace kintsugi::core
