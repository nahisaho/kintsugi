#include "../third_party/doctest/doctest.h"
#include "../src/core/pose_conflict.hpp"

using kintsugi::core::PropagatedPose;
using kintsugi::core::Quaternion;
using kintsugi::core::Vec3;
using kintsugi::core::isConflicting;

/** @id TEST-POTTERY-004-001
 * @verifies REQ-POTTERY-027
 */
// 並進差が1.0mmを超え、回転差が2度以内のとき、姿勢矛盾と判定されること。
TEST_CASE("TEST-POTTERY-004-001 translation difference over 1.0mm is a conflict") {
  PropagatedPose poseA{Vec3{0.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
  PropagatedPose poseB{Vec3{1.5, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};

  CHECK(isConflicting(poseA, poseB) == true);
}
