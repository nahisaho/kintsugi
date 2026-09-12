#include <algorithm>
#include <optional>

#include "../third_party/doctest/doctest.h"
#include "../src/core/assembly_orchestrator.hpp"
#include "../src/core/fragment_matching.hpp"
#include "../src/core/fragment_mesh.hpp"
#include "../src/core/pose_conflict.hpp"
#include "../src/core/report_aggregation.hpp"

using kintsugi::core::AssemblyOrchestrator;
using kintsugi::core::AssemblyState;
using kintsugi::core::FragmentMesh;
using kintsugi::core::FragmentReference;
using kintsugi::core::JoinCandidate;
using kintsugi::core::JoinState;
using kintsugi::core::PropagatedPose;
using kintsugi::core::Quaternion;
using kintsugi::core::Vec3;

namespace {

const PropagatedPose kIdentityPose{Vec3{0.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};

JoinCandidate makeCandidate(std::size_t a, std::size_t b, const PropagatedPose& pose, double confidence) {
  JoinCandidate candidate;
  candidate.fragmentIdA = a;
  candidate.fragmentIdB = b;
  candidate.pose = pose;
  candidate.confidenceScore = confidence;
  return candidate;
}

bool containsReference(const std::vector<FragmentReference>& refs, std::optional<std::size_t> clusterIndex,
                        std::size_t fragmentId) {
  for (const auto& ref : refs) {
    if (ref.clusterIndex == clusterIndex && ref.fragmentId == fragmentId) {
      return true;
    }
  }
  return false;
}

}  // namespace

/** @id TEST-POTTERY-009-001
 * @verifies REQ-POTTERY-009
 */
// 器物候補内で採用済み接合を持たない破片、および未分類破片の全件が未接合破片
// 一覧に含まれ、採用済み接合を持つ破片は含まれないこと。
TEST_CASE("TEST-POTTERY-009-001 buildUnmatchedFragmentReport aggregates cluster-internal and unclassified unmatched fragments") {
  // 器物候補: 破片0-1が接合済み、破片2は接合先なし。
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1, 2});
  orchestrator.runFullAuto(std::vector<JoinCandidate>{makeCandidate(0, 1, kIdentityPose, 90.0)});

  std::vector<AssemblyState> assemblyStates{orchestrator.state()};
  std::vector<FragmentMesh> unclassified(2);  // 未分類破片2件。

  const auto report = buildUnmatchedFragmentReport(assemblyStates, unclassified);

  CHECK(containsReference(report.unmatchedFragments, 0, 2) == true);
  CHECK(containsReference(report.unmatchedFragments, std::nullopt, 0) == true);
  CHECK(containsReference(report.unmatchedFragments, std::nullopt, 1) == true);
  CHECK(containsReference(report.unmatchedFragments, 0, 0) == false);
  CHECK(containsReference(report.unmatchedFragments, 0, 1) == false);
  CHECK(report.unmatchedFragments.size() == 3);
}

/** @id TEST-POTTERY-013-001
 * @verifies REQ-POTTERY-013
 */
// 複数の器物候補にまたがる採用済み接合の全件が、対応する一致度スコアとともに
// 接合箇所レポートに含まれること。
TEST_CASE("TEST-POTTERY-013-001 buildJoinReport lists every accepted join across clusters with its confidence score") {
  AssemblyOrchestrator orchestratorA(std::vector<std::size_t>{0, 1});
  orchestratorA.runFullAuto(std::vector<JoinCandidate>{makeCandidate(0, 1, kIdentityPose, 85.0)});

  AssemblyOrchestrator orchestratorB(std::vector<std::size_t>{0, 1, 2});
  orchestratorB.runFullAuto(std::vector<JoinCandidate>{makeCandidate(1, 2, kIdentityPose, 77.0)});

  std::vector<AssemblyState> assemblyStates{orchestratorA.state(), orchestratorB.state()};
  const auto report = buildJoinReport(assemblyStates);

  REQUIRE(report.joins.size() == 2);
  bool foundClusterAJoin = false;
  bool foundClusterBJoin = false;
  for (const auto& entry : report.joins) {
    if (entry.clusterIndex == 0 && entry.fragmentIdA == 0 && entry.fragmentIdB == 1) {
      CHECK(entry.confidenceScore == doctest::Approx(85.0));
      foundClusterAJoin = true;
    }
    if (entry.clusterIndex == 1 && entry.fragmentIdA == 1 && entry.fragmentIdB == 2) {
      CHECK(entry.confidenceScore == doctest::Approx(77.0));
      foundClusterBJoin = true;
    }
  }
  CHECK(foundClusterAJoin == true);
  CHECK(foundClusterBJoin == true);
}

/** @id TEST-POTTERY-012-001
 * @verifies REQ-POTTERY-012
 */
// インポート済み全破片（器物候補内破片＋未分類破片）の識別子・変換行列・接合状態
// （確定済み／未接合）が漏れなく出力データに含まれること。
TEST_CASE("TEST-POTTERY-012-001 buildPoseExport includes every imported fragment with identifier, pose, and join state") {
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1, 2});
  orchestrator.runFullAuto(std::vector<JoinCandidate>{makeCandidate(0, 1, kIdentityPose, 92.0)});

  std::vector<AssemblyState> assemblyStates{orchestrator.state()};
  std::vector<FragmentMesh> unclassified(1);

  const auto exportData = buildPoseExport(assemblyStates, unclassified);

  // インポート済み全破片（器物候補内3件＋未分類1件＝4件）が漏れなく含まれること。
  REQUIRE(exportData.fragments.size() == 4);

  auto findEntry = [&](std::optional<std::size_t> clusterIndex, std::size_t fragmentId) {
    return std::find_if(exportData.fragments.begin(), exportData.fragments.end(), [&](const auto& entry) {
      return entry.fragment.clusterIndex == clusterIndex && entry.fragment.fragmentId == fragmentId;
    });
  };

  const auto entry0 = findEntry(0, 0);
  const auto entry1 = findEntry(0, 1);
  const auto entry2 = findEntry(0, 2);
  const auto entryUnclassified = findEntry(std::nullopt, 0);
  REQUIRE(entry0 != exportData.fragments.end());
  REQUIRE(entry1 != exportData.fragments.end());
  REQUIRE(entry2 != exportData.fragments.end());
  REQUIRE(entryUnclassified != exportData.fragments.end());

  CHECK(entry0->joinState == JoinState::Confirmed);
  CHECK(entry1->joinState == JoinState::Confirmed);
  CHECK(entry2->joinState == JoinState::Unmatched);
  CHECK(entryUnclassified->joinState == JoinState::Unmatched);
}
