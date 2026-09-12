#include <cmath>

#include "../third_party/doctest/doctest.h"
#include "../src/core/assembly_orchestrator.hpp"
#include "../src/core/fragment_mesh.hpp"
#include "../src/core/missing_part_estimation.hpp"

using kintsugi::core::AssemblyOrchestrator;
using kintsugi::core::estimateMissingParts;
using kintsugi::core::FragmentMesh;
using kintsugi::core::PropagatedPose;
using kintsugi::core::Quaternion;
using kintsugi::core::Vec3;

namespace {

constexpr double kPi = 3.14159265358979323846;

// 半径radius・高さ[-halfHeight, halfHeight]・角度[angleStartDeg, angleEndDeg]の
// 円柱側面（部分シェル）を稠密にサンプリングした合成破片メッシュを生成する。
FragmentMesh makeCylindricalShellFragment(double radius, double halfHeight, double angleStartDeg,
                                           double angleEndDeg, int heightSteps, int angleSteps) {
  FragmentMesh mesh;
  for (int hi = 0; hi <= heightSteps; ++hi) {
    const double h = -halfHeight + (2.0 * halfHeight) * hi / heightSteps;
    for (int ai = 0; ai <= angleSteps; ++ai) {
      const double angleDeg = angleStartDeg + (angleEndDeg - angleStartDeg) * ai / angleSteps;
      const double angleRad = angleDeg * kPi / 180.0;
      mesh.vertices.push_back(Vec3{radius * std::cos(angleRad), radius * std::sin(angleRad), h});
    }
  }
  return mesh;
}

// 回転対称性を持たない不規則な合成破片メッシュ（決定的疑似乱数によるブロック状の
// 点群）を生成する。
FragmentMesh makeIrregularBlobFragment(std::uint32_t seed) {
  FragmentMesh mesh;
  std::uint32_t state = seed;
  auto nextRandom01 = [&state]() -> double {
    // 決定的な疑似乱数（線形合同法）。テストの再現性を保つため std::rand は使わない。
    state = state * 1664525u + 1013904223u;
    return static_cast<double>(state) / static_cast<double>(0xFFFFFFFFu);
  };
  for (int i = 0; i < 200; ++i) {
    const double x = (nextRandom01() - 0.5) * 100.0;
    const double y = (nextRandom01() - 0.5) * 100.0;
    const double z = (nextRandom01() - 0.5) * 100.0;
    mesh.vertices.push_back(Vec3{x, y, z});
  }
  return mesh;
}

const PropagatedPose kIdentityPose{Vec3{0.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};

}  // namespace

/** @id TEST-POTTERY-010-001
 * @verifies REQ-POTTERY-010
 */
// 回転対称形状（円柱側面の半分のみが残存）に対し、欠損部分推定は実測データが
// 存在しない角度区間（残り半分）にのみ推定形状の頂点を生成し、実測データが
// 存在する角度区間には生成しないこと。
TEST_CASE("TEST-POTTERY-010-001 estimateMissingParts fills only the missing angular range of a partial cylinder") {
  std::vector<FragmentMesh> fragments;
  // 破片0: 半径30mm・高さ[-100,100]mm（壺状の縦長形状）・角度[0,300]度
  // （周方向の一部、60度分の欠損くさび形のみが未実測）。高さ方向の広がりが
  // 直径より十分大きいため、PCAによる回転軸推定は高さ方向(Z軸)を正しく
  // 検出できる。
  fragments.push_back(makeCylindricalShellFragment(30.0, 100.0, 0.0, 300.0, 20, 60));

  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0});
  orchestrator.applyManualTransform(0, kIdentityPose);

  const auto result = estimateMissingParts(orchestrator.state(), fragments);
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  const auto& record = (*result)[0];
  REQUIRE(record.vertices.size() > 0);

  // 生成された推定頂点はすべて欠損区間（角度300〜360度側）に属し、実測区間
  // （角度0〜300度側、ある程度のマージンを設ける）には生成されていないこと。
  bool anyVertexInMeasuredRange = false;
  for (const Vec3& v : record.vertices) {
    double angleRad = std::atan2(v.y, v.x);
    if (angleRad < 0.0) angleRad += 2.0 * kPi;
    const double angleDeg = angleRad * 180.0 / kPi;
    // 実測区間(0〜300度)からマージン15度を除いた範囲(15〜285度)に生成されて
    // いないことを確認する。
    if (angleDeg > 15.0 && angleDeg < 285.0) {
      anyVertexInMeasuredRange = true;
    }
  }
  CHECK(anyVertexInMeasuredRange == false);
}

/** @id TEST-POTTERY-010-002
 * @verifies REQ-POTTERY-010
 */
// 回転対称と判定できない不規則形状に対しては欠損部分推定を行わず、
// std::nulloptを返すこと（optional-feature、機能無効相当の扱い）。
TEST_CASE("TEST-POTTERY-010-002 estimateMissingParts returns nullopt for a non-symmetric shape") {
  std::vector<FragmentMesh> fragments;
  fragments.push_back(makeIrregularBlobFragment(12345u));

  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0});
  orchestrator.applyManualTransform(0, kIdentityPose);

  const auto result = estimateMissingParts(orchestrator.state(), fragments);
  CHECK(result.has_value() == false);
}
