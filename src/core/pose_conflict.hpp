#pragma once

namespace kintsugi::core {

// 3次元並進ベクトル（単位: ミリメートル）。
struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

// 回転を表す単位四元数（w, x, y, z）。
struct Quaternion {
  double w = 1.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

// 同一組み立て済み部分の基準座標系へ伝播済みの推定姿勢。
struct PropagatedPose {
  Vec3 translationMm;
  Quaternion rotation;
};

// REQ-POTTERY-027の並進差（mm）を算出する。
double translationDifferenceMm(const PropagatedPose& poseA, const PropagatedPose& poseB);

// REQ-POTTERY-027の回転差（度、最短相対回転角）を算出する。
double rotationDifferenceDegrees(const PropagatedPose& poseA, const PropagatedPose& poseB);

/** @id CODE-POTTERY-004
 * @implements REQ-POTTERY-027
 * @design DES-POTTERY-004
 */
// REQ-POTTERY-027の基準（並進差>1.0mm、または回転差>2度）で姿勢矛盾を判定する。
bool isConflicting(const PropagatedPose& poseA, const PropagatedPose& poseB);

}  // namespace kintsugi::core
