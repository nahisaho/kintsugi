#include "../third_party/doctest/doctest.h"
#include "../src/core/fragment_clustering.hpp"

#include <algorithm>
#include <set>

using kintsugi::core::ClusteringResult;
using kintsugi::core::ColorRGB;
using kintsugi::core::FragmentMesh;
using kintsugi::core::Vec3;
using kintsugi::core::VesselCluster;
using kintsugi::core::clusterFragments;

namespace {

// 厚みthicknessMm・頂点色colorの直方体破片を模した合成FragmentMeshを生成する。
// PFR-BENCH-CLUSTER-001（正解ラベル付き複数器物混在破片群）の代替として、
// 実測スキャンデータが存在しないため本テスト内で合成した固定データセットを用いる。
FragmentMesh makeSyntheticFragment(double widthMm, double heightMm, double thicknessMm,
                                    ColorRGB color) {
  FragmentMesh mesh;
  mesh.vertices = {
      Vec3{0.0, 0.0, 0.0},
      Vec3{widthMm, 0.0, 0.0},
      Vec3{widthMm, heightMm, 0.0},
      Vec3{0.0, heightMm, 0.0},
      Vec3{0.0, 0.0, thicknessMm},
      Vec3{widthMm, 0.0, thicknessMm},
      Vec3{widthMm, heightMm, thicknessMm},
      Vec3{0.0, heightMm, thicknessMm},
  };
  mesh.colors.assign(mesh.vertices.size(), color);
  return mesh;
}

}  // namespace

/** @id TEST-POTTERY-003-001
 * @verifies REQ-POTTERY-003
 */
TEST_CASE(
    "TEST-POTTERY-003-001: "
    "PFR-BENCH-CLUSTER-001代替データセットで正解ラベルとの一致率が95%以上となる") {
  // 器物A: 肉厚5mm・赤系の破片5点（正解ラベルA）
  std::vector<FragmentMesh> fragments;
  const ColorRGB red{200, 50, 50};
  const ColorRGB blue{50, 50, 200};
  for (int i = 0; i < 5; ++i) {
    fragments.push_back(
        makeSyntheticFragment(20.0 + i, 15.0, 5.0 + 0.1 * i, ColorRGB{static_cast<std::uint8_t>(200 - i),
                                                                       static_cast<std::uint8_t>(50 + i),
                                                                       static_cast<std::uint8_t>(50 + i)}));
  }
  // 器物B: 肉厚9mm・青系の破片5点（正解ラベルB）
  for (int i = 0; i < 5; ++i) {
    fragments.push_back(makeSyntheticFragment(
        18.0 + i, 12.0, 9.0 + 0.1 * i,
        ColorRGB{static_cast<std::uint8_t>(50 + i), static_cast<std::uint8_t>(50 + i),
                 static_cast<std::uint8_t>(200 - i)}));
  }

  std::vector<int> groundTruthLabel(10);
  for (int i = 0; i < 5; ++i) groundTruthLabel[i] = 0;
  for (int i = 5; i < 10; ++i) groundTruthLabel[i] = 1;

  ClusteringResult result = clusterFragments(fragments);

  REQUIRE(result.unclassified.empty());
  REQUIRE(result.vesselCandidates.size() == 2);

  // 各クラスタ内の破片が同一正解ラベルに属するか（一致率）を検証する。
  int totalFragments = 0;
  int correctlyGrouped = 0;
  for (const auto& cluster : result.vesselCandidates) {
    std::vector<int> labelsInCluster;
    for (const auto& fragment : cluster.fragments) {
      // 色の平均から元の破片を特定し、正解ラベルを引き当てる。
      for (std::size_t i = 0; i < fragments.size(); ++i) {
        if (fragments[i].vertices.size() == fragment.vertices.size() &&
            fragments[i].colors.front().r == fragment.colors.front().r &&
            fragments[i].colors.front().b == fragment.colors.front().b) {
          labelsInCluster.push_back(groundTruthLabel[i]);
          break;
        }
      }
    }
    REQUIRE(labelsInCluster.size() == cluster.fragments.size());
    const int majorityLabel = labelsInCluster.front();
    for (int label : labelsInCluster) {
      ++totalFragments;
      if (label == majorityLabel) {
        ++correctlyGrouped;
      }
    }
    for (double confidence : cluster.membershipConfidence) {
      CHECK(confidence >= 70.0);
      CHECK(confidence <= 100.0);
    }
  }

  REQUIRE(totalFragments == 10);
  const double agreementRate = static_cast<double>(correctlyGrouped) / totalFragments;
  CHECK(agreementRate >= 0.95);
}

/** @id TEST-POTTERY-003-002
 * @verifies REQ-POTTERY-003
 */
TEST_CASE("TEST-POTTERY-003-002: 所属信頼度70未満の破片は未分類破片として区別される") {
  std::vector<FragmentMesh> fragments;
  const ColorRGB red{200, 50, 50};
  // 器物A: 肉厚5mm・赤系の破片3点。
  for (int i = 0; i < 3; ++i) {
    fragments.push_back(makeSyntheticFragment(20.0 + i, 15.0, 5.0, red));
  }
  // 孤立破片: 他のどの破片とも肉厚・色が大きく異なり、信頼度70未満となるべき。
  const ColorRGB green{40, 200, 40};
  fragments.push_back(makeSyntheticFragment(50.0, 40.0, 25.0, green));

  ClusteringResult result = clusterFragments(fragments);

  REQUIRE(result.vesselCandidates.size() == 1);
  CHECK(result.vesselCandidates.front().fragments.size() == 3);
  REQUIRE(result.unclassified.size() == 1);
  CHECK(result.unclassified.front().colors.front().g == 200);
}
