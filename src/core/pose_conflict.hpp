#pragma once

#include <cstddef>
#include <unordered_map>

namespace kintsugi::core {

// DES-POTTERY-003（fragment_matching.hpp）の前方宣言。full定義への依存を避け、
// 本ヘッダはPropagatedPose算出に必要な最小限のインターフェースのみを公開する。
struct JoinCandidate;

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

// fragmentIdをキーとする、組み立て済み部分の基準座標系での確立済み推定姿勢。
// 未登録の破片IDは恒等姿勢（未組み立て・基準そのもの）とみなす。
using FragmentAnchorMap = std::unordered_map<std::size_t, PropagatedPose>;

// outerの座標系でinnerを合成する（outer ∘ inner）。姿勢の連鎖的な伝播に用いる。
PropagatedPose composePoses(const PropagatedPose& outer, const PropagatedPose& inner);

// poseの逆変換を返す（q^-1, -(q^-1・t)）。
PropagatedPose invertPose(const PropagatedPose& pose);

/** @id CODE-POTTERY-012
 * @implements REQ-POTTERY-027
 * @design DES-POTTERY-004
 */
// candidateが提示するfragmentIdA→fragmentIdBの相対姿勢(candidate.pose)を、
// anchors内のfragmentIdAの既知の基準座標系姿勢（未登録なら恒等姿勢）に合成し、
// fragmentIdBの基準座標系における推定姿勢を算出する（DES-POTTERY-004）。
PropagatedPose propagatePose(const JoinCandidate& candidate, const FragmentAnchorMap& anchors);

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
