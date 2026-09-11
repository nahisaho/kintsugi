#include "pose_conflict.hpp"

#include <algorithm>
#include <cmath>

namespace kintsugi::core {

namespace {
constexpr double kTranslationConflictThresholdMm = 1.0;
constexpr double kRotationConflictThresholdDegrees = 2.0;
constexpr double kPi = 3.14159265358979323846;
}  // namespace

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
