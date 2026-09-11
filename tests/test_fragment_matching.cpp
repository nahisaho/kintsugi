#include "../third_party/doctest/doctest.h"
#include "../src/core/fragment_matching.hpp"

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <set>
#include <utility>

using kintsugi::core::ColorRGB;
using kintsugi::core::FragmentMesh;
using kintsugi::core::JoinCandidate;
using kintsugi::core::Vec3;
using kintsugi::core::computeJoinCandidates;

namespace {

// 破断面固有の微細凹凸（seed依存ノイズ）を持つ曲面パッチを合成する。
// PFR-BENCH-001（正解接合ラベル付きデータセット）の実測データが存在しないため、
// 破片の破断面点群を模した合成データセットを本テスト内で構築する。
FragmentMesh makeFracturePatch(double radius, double arcDeg, int n, double zScale, int seed,
                                std::optional<ColorRGB> color = std::nullopt) {
  FragmentMesh mesh;
  for (int i = 0; i < n; ++i) {
    const double t = arcDeg * M_PI / 180.0 * static_cast<double>(i) / (n - 1);
    for (int j = 0; j < n; ++j) {
      const double z = zScale * static_cast<double>(j) / (n - 1);
      const double bump = 3.0 * std::sin(2.7 * i + seed * 1.3) * std::cos(1.9 * j + seed * 0.7) +
                           2.0 * std::sin(0.6 * i * j + seed * 2.1);
      Vec3 v;
      v.x = (radius + bump) * std::cos(t);
      v.y = (radius + bump) * std::sin(t);
      v.z = z + 0.5 * bump;
      mesh.vertices.push_back(v);
    }
  }
  if (color.has_value()) {
    mesh.colors.assign(mesh.vertices.size(), *color);
  }
  return mesh;
}

// 破片の頂点群へ剛体変換（回転＋並進）を適用した複製を返す。
FragmentMesh applyRigidTransform(const FragmentMesh& mesh, double tx, double ty, double tz,
                                  double rotationZRad) {
  FragmentMesh out = mesh;
  const double cosT = std::cos(rotationZRad);
  const double sinT = std::sin(rotationZRad);
  for (auto& v : out.vertices) {
    const double x = v.x * cosT - v.y * sinT;
    const double y = v.x * sinT + v.y * cosT;
    v.x = x + tx;
    v.y = y + ty;
    v.z = v.z + tz;
  }
  return out;
}

}  // namespace

/** @id TEST-POTTERY-004-002
 * @verifies REQ-POTTERY-004
 */
TEST_CASE("TEST-POTTERY-004-002: 破片の組ごとに接合候補は常に1件のみ算出される") {
  std::vector<FragmentMesh> fragments;
  auto base = makeFracturePatch(30.0, 40.0, 6, 15.0, 1);
  fragments.push_back(base);
  fragments.push_back(applyRigidTransform(base, 5.0, -3.0, 2.0, 0.3));
  fragments.push_back(makeFracturePatch(30.0, 40.0, 6, 15.0, 2));

  std::vector<JoinCandidate> candidates = computeJoinCandidates(fragments);

  // 3破片 = 3ペア。各ペアにつき候補は1件のみ。
  REQUIRE(candidates.size() == 3);
  std::set<std::pair<std::size_t, std::size_t>> seenPairs;
  for (const auto& candidate : candidates) {
    auto key = std::make_pair(candidate.fragmentIdA, candidate.fragmentIdB);
    CHECK(seenPairs.find(key) == seenPairs.end());
    seenPairs.insert(key);
  }

  // 同一破断面（複製＋剛体変換）由来の対は、無関係な破片対より明確に高い信頼度となる。
  double matchingConfidence = -1.0;
  double unrelatedConfidence = -1.0;
  for (const auto& candidate : candidates) {
    if (candidate.fragmentIdA == 0 && candidate.fragmentIdB == 1) {
      matchingConfidence = candidate.confidenceScore;
    }
    if (candidate.fragmentIdA == 0 && candidate.fragmentIdB == 2) {
      unrelatedConfidence = candidate.confidenceScore;
    }
  }
  REQUIRE(matchingConfidence >= 0.0);
  REQUIRE(unrelatedConfidence >= 0.0);
  CHECK(matchingConfidence >= 70.0);
  CHECK(unrelatedConfidence < 70.0);
}

/** @id TEST-POTTERY-018-001
 * @verifies REQ-POTTERY-018, REQ-POTTERY-025, REQ-POTTERY-028
 */
TEST_CASE(
    "TEST-POTTERY-018-001: "
    "PFR-BENCH-001代替データセットで採用閾値70以上の候補の適合率90%以上・再現率85%以上・信頼度中央値分離を満たす") {
  // 5破片の破断面パッチ（各々固有の凹凸seedを持つ）。
  std::vector<FragmentMesh> baseFragments;
  for (int seed = 1; seed <= 5; ++seed) {
    baseFragments.push_back(makeFracturePatch(30.0, 40.0, 6, 15.0, seed));
  }

  // 正解接合ペア: (0,1),(2,3)。各ペアは同一破断面（同一seed由来の複製）を
  // 共有する構成とし、それ以外の組み合わせは互いに独立したseedの破片同士とする。
  std::vector<FragmentMesh> fragments;
  fragments.push_back(baseFragments[0]);
  fragments.push_back(applyRigidTransform(baseFragments[0], 5.0, -3.0, 2.0, 0.3));
  fragments.push_back(baseFragments[1]);
  fragments.push_back(applyRigidTransform(baseFragments[1], -4.0, 6.0, -1.0, 0.5));
  fragments.push_back(baseFragments[2]);

  const std::set<std::pair<std::size_t, std::size_t>> correctPairs = {
      {0, 1}, {2, 3}};

  std::vector<JoinCandidate> candidates = computeJoinCandidates(fragments);
  REQUIRE(candidates.size() == 10);  // C(5,2)

  std::vector<double> correctConfidences;
  std::vector<double> incorrectConfidences;
  int adoptedTotal = 0;
  int adoptedCorrect = 0;
  for (const auto& candidate : candidates) {
    CHECK(candidate.confidenceScore >= 0.0);
    CHECK(candidate.confidenceScore <= 100.0);
    const auto key = std::make_pair(candidate.fragmentIdA, candidate.fragmentIdB);
    const bool isCorrect = correctPairs.count(key) > 0;
    (isCorrect ? correctConfidences : incorrectConfidences).push_back(candidate.confidenceScore);
    if (candidate.confidenceScore >= 70.0) {
      ++adoptedTotal;
      if (isCorrect) ++adoptedCorrect;
    }
  }

  REQUIRE(adoptedTotal > 0);
  const double precision = static_cast<double>(adoptedCorrect) / adoptedTotal;
  const double recall = static_cast<double>(adoptedCorrect) / correctPairs.size();
  CHECK(precision >= 0.90);
  CHECK(recall >= 0.85);

  const auto median = [](std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
  };
  CHECK(median(correctConfidences) > median(incorrectConfidences));
}

/** @id TEST-POTTERY-005-001
 * @verifies REQ-POTTERY-005
 */
TEST_CASE("TEST-POTTERY-005-001: 色・模様情報が付与されている場合、色不一致の破片対は信頼度が低下する") {
  auto shapeBase = makeFracturePatch(30.0, 40.0, 6, 15.0, 1);

  const ColorRGB red{200, 40, 40};
  const ColorRGB blue{40, 40, 200};

  FragmentMesh a = shapeBase;
  a.colors.assign(a.vertices.size(), red);
  FragmentMesh bMatchingColor = applyRigidTransform(shapeBase, 5.0, -3.0, 2.0, 0.3);
  bMatchingColor.colors.assign(bMatchingColor.vertices.size(), red);
  FragmentMesh bMismatchedColor = applyRigidTransform(shapeBase, 5.0, -3.0, 2.0, 0.3);
  bMismatchedColor.colors.assign(bMismatchedColor.vertices.size(), blue);

  std::vector<JoinCandidate> matchingColorResult = computeJoinCandidates({a, bMatchingColor});
  std::vector<JoinCandidate> mismatchedColorResult = computeJoinCandidates({a, bMismatchedColor});

  REQUIRE(matchingColorResult.size() == 1);
  REQUIRE(mismatchedColorResult.size() == 1);
  REQUIRE(matchingColorResult.front().evidence.colorScore.has_value());
  REQUIRE(mismatchedColorResult.front().evidence.colorScore.has_value());

  // 形状は同一（shapeScoreはほぼ同等）だが、色が一致しないペアは色スコアの低下により
  // 総合信頼度が色一致ペアより低くなる。
  CHECK(mismatchedColorResult.front().evidence.colorScore.value() <
        matchingColorResult.front().evidence.colorScore.value());
  CHECK(mismatchedColorResult.front().confidenceScore < matchingColorResult.front().confidenceScore);
}
