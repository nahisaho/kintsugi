#include "../third_party/doctest/doctest.h"
#include "../src/core/fragment_matching.hpp"
#include "../src/core/pose_conflict.hpp"

using kintsugi::core::FragmentAnchorMap;
using kintsugi::core::JoinCandidate;
using kintsugi::core::PropagatedPose;
using kintsugi::core::Quaternion;
using kintsugi::core::Vec3;
using kintsugi::core::isConflicting;
using kintsugi::core::propagatePose;

/** @id TEST-POTTERY-027-001
 * @verifies REQ-POTTERY-027
 */
// 並進差が1.0mmを超え、回転差が2度以内のとき、姿勢矛盾と判定されること。
TEST_CASE("TEST-POTTERY-027-001 translation difference over 1.0mm is a conflict") {
  PropagatedPose poseA{Vec3{0.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
  PropagatedPose poseB{Vec3{1.5, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};

  CHECK(isConflicting(poseA, poseB) == true);
}

/** @id TEST-POTTERY-027-002
 * @verifies REQ-POTTERY-027
 */
// fragmentIdAが既に基準座標系に組み込み済み（anchors登録済み）のとき、
// propagatePoseはfragmentIdAの確立済み姿勢に候補の相対姿勢を合成した
// fragmentIdBの推定姿勢を返すこと。
TEST_CASE("TEST-POTTERY-027-002 propagatePose composes candidate pose with anchored fragment pose") {
  // fragmentId=1は既にX軸+10mm・Z軸周り90度回転した状態で組み立て済みとする。
  const double kHalfSqrt2 = 0.70710678118654752440;
  PropagatedPose anchorPoseOfA{Vec3{10.0, 0.0, 0.0}, Quaternion{kHalfSqrt2, 0.0, 0.0, kHalfSqrt2}};
  FragmentAnchorMap anchors;
  anchors[1] = anchorPoseOfA;

  // 候補: fragmentId=1から見てfragmentId=2はX軸+5mmの位置にある（回転なし）。
  JoinCandidate candidate;
  candidate.fragmentIdA = 1;
  candidate.fragmentIdB = 2;
  candidate.pose = PropagatedPose{Vec3{5.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};

  const PropagatedPose propagated = propagatePose(candidate, anchors);

  // 期待値: Aの姿勢(90度回転+X10mm平行移動)にAから見た相対位置(+X5mm)を合成すると、
  // 90度回転によりX軸は+Y軸方向へ写るため、基準座標系ではY+5mm・X+10mmとなる。
  CHECK(propagated.translationMm.x == doctest::Approx(10.0).epsilon(0.001));
  CHECK(propagated.translationMm.y == doctest::Approx(5.0).epsilon(0.001));
  CHECK(propagated.translationMm.z == doctest::Approx(0.0).epsilon(0.001));
  CHECK(propagated.rotation.w == doctest::Approx(kHalfSqrt2).epsilon(0.001));
  CHECK(propagated.rotation.z == doctest::Approx(kHalfSqrt2).epsilon(0.001));

  // anchors未登録のfragmentIdをfragmentIdAとする候補は、恒等姿勢を基準として
  // 候補の相対姿勢そのものが伝播結果となること。
  JoinCandidate unanchoredCandidate;
  unanchoredCandidate.fragmentIdA = 99;
  unanchoredCandidate.fragmentIdB = 100;
  unanchoredCandidate.pose = PropagatedPose{Vec3{3.0, 4.0, 5.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
  const PropagatedPose propagatedFromUnanchored = propagatePose(unanchoredCandidate, anchors);
  CHECK(propagatedFromUnanchored.translationMm.x == doctest::Approx(3.0).epsilon(0.001));
  CHECK(propagatedFromUnanchored.translationMm.y == doctest::Approx(4.0).epsilon(0.001));
  CHECK(propagatedFromUnanchored.translationMm.z == doctest::Approx(5.0).epsilon(0.001));
}
