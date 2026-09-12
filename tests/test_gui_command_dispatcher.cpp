#include "../third_party/doctest/doctest.h"
#include "../src/core/assembly_orchestrator.hpp"
#include "../src/core/fragment_matching.hpp"
#include "../src/core/pose_conflict.hpp"
#include "../src/gui/gui_command_dispatcher.hpp"

using kintsugi::core::AssemblyOrchestrator;
using kintsugi::core::JoinCandidate;
using kintsugi::core::PropagatedPose;
using kintsugi::core::Quaternion;
using kintsugi::core::Vec3;
using kintsugi::gui::GuiCommandDispatcher;

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

}  // namespace

/** @id TEST-POTTERY-017-001
 * @verifies REQ-POTTERY-017
 */
// GUIコマンドディスパッチが、候補採否・手動微調整・undo/redoの各UI操作を
// DES-POTTERY-005（AssemblyOrchestrator）へ正しく委譲し、組み立て結果へ反映
// すること（GUI非依存の形での主要機能操作性の確認、REQ-POTTERY-007/008と
// 同様のスコープ決定）。
TEST_CASE("TEST-POTTERY-017-001 GuiCommandDispatcher delegates accept/reject/drag/undo/redo to the assembly orchestrator") {
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1});
  GuiCommandDispatcher dispatcher(orchestrator);

  const auto candidate = makeCandidate(0, 1, kIdentityPose, 92.0);

  SUBCASE("accepting a non-conflicting candidate updates the assembly state with no conflict warning") {
    const auto& state = dispatcher.acceptCandidate(candidate);
    REQUIRE(state.acceptedJoins.size() == 1);
    CHECK(state.acceptedJoins[0].fragmentIdA == 0);
    CHECK(state.acceptedJoins[0].fragmentIdB == 1);
    CHECK(dispatcher.lastConflictWarning().has_value() == false);
  }

  SUBCASE("rejecting a candidate records it as a user-rejected candidate") {
    const auto& state = dispatcher.rejectCandidate(candidate);
    REQUIRE(state.rejectedCandidates.size() == 1);
    CHECK(state.rejectedCandidates[0].reason == kintsugi::core::RejectionReason::UserRejected);
  }

  SUBCASE("a manual drag operation is reflected as a manual pose override") {
    const PropagatedPose dragged{Vec3{12.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
    const auto& state = dispatcher.applyManualDrag(0, dragged);
    REQUIRE(state.manualPoseOverrides.count(0) == 1);
    CHECK(state.manualPoseOverrides.at(0).translationMm.x == doctest::Approx(12.0));
  }

  SUBCASE("undo reverts the last dispatched operation and redo reapplies it") {
    dispatcher.acceptCandidate(candidate);
    const auto& afterUndo = dispatcher.undo();
    CHECK(afterUndo.acceptedJoins.empty());
    const auto& afterRedo = dispatcher.redo();
    CHECK(afterRedo.acceptedJoins.size() == 1);
  }

  SUBCASE("accepting a conflicting candidate surfaces a conflict warning and leaves the state unchanged") {
    AssemblyOrchestrator conflictOrchestrator(std::vector<std::size_t>{0, 1, 2});
    GuiCommandDispatcher conflictDispatcher(conflictOrchestrator);

    const PropagatedPose poseAB{Vec3{10.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
    const PropagatedPose poseAC{Vec3{999.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
    conflictDispatcher.acceptCandidate(makeCandidate(0, 1, poseAB, 90.0));
    // fragmentIdA=0を基準として、既に採用済みのA-Bとは全く異なる姿勢を要求する
    // A-C候補は、fragmentId=0の確立済み姿勢(恒等姿勢)と矛盾しない前提だが、
    // ここではB自体を基準とする別の矛盾ケースを構成する: 破片1(B)を基準に
    // 破片0(A)へ別の姿勢を要求する候補は、既にA-Bで確立された0の姿勢と
    // 矛盾するはずである。
    const auto conflictCandidate = makeCandidate(1, 0, poseAC, 85.0);
    const std::size_t acceptedCountBefore = conflictOrchestrator.state().acceptedJoins.size();
    conflictDispatcher.acceptCandidate(conflictCandidate);

    REQUIRE(conflictDispatcher.lastConflictWarning().has_value());
    CHECK(conflictOrchestrator.state().acceptedJoins.size() == acceptedCountBefore);
  }
}
