#include "../third_party/doctest/doctest.h"
#include "../src/core/assembly_orchestrator.hpp"
#include "../src/core/fragment_matching.hpp"
#include "../src/core/pose_conflict.hpp"

using kintsugi::core::AcceptedJoin;
using kintsugi::core::AssemblyOrchestrator;
using kintsugi::core::AssemblyState;
using kintsugi::core::CandidateListView;
using kintsugi::core::ConflictWarning;
using kintsugi::core::JoinCandidate;
using kintsugi::core::PropagatedPose;
using kintsugi::core::Quaternion;
using kintsugi::core::RejectionReason;
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

}  // namespace

/** @id TEST-POTTERY-006-001
 * @verifies REQ-POTTERY-006
 */
// 完全自動モードは信頼度70以上の候補を信頼度降順で採用し、既採用候補と矛盾する
// 候補は却下候補として記録し、信頼度70未満の候補は採否判定の対象外とすること。
// また、いかなる破片も一覧から消失しないこと。
TEST_CASE("TEST-POTTERY-006-001 runFullAuto accepts non-conflicting high-confidence candidates and rejects conflicts") {
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1, 2, 3, 4});

  std::vector<JoinCandidate> candidates;
  // 0-1-2-3の鎖: 各段でX軸+10mmずつ離れていく一貫した姿勢。
  candidates.push_back(makeCandidate(0, 1, PropagatedPose{Vec3{10.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 90.0));
  candidates.push_back(makeCandidate(1, 2, PropagatedPose{Vec3{10.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 85.0));
  candidates.push_back(makeCandidate(2, 3, PropagatedPose{Vec3{10.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 75.0));
  // 0-2の直接候補: 鎖からの伝播姿勢(X+20mm)と矛盾するY軸+10mmの姿勢を提示する。
  candidates.push_back(makeCandidate(0, 2, PropagatedPose{Vec3{0.0, 10.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 80.0));
  // 信頼度70未満: 採否判定の対象外。
  candidates.push_back(makeCandidate(0, 4, PropagatedPose{Vec3{1.0, 1.0, 1.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 50.0));

  const AssemblyState& state = orchestrator.runFullAuto(candidates);

  CHECK(state.fragmentIds.size() == 5);
  CHECK(state.acceptedJoins.size() == 3);
  REQUIRE(state.rejectedCandidates.size() == 1);
  CHECK(state.rejectedCandidates[0].fragmentIdA == 0);
  CHECK(state.rejectedCandidates[0].fragmentIdB == 2);
  CHECK(state.rejectedCandidates[0].reason == RejectionReason::Conflict);
  CHECK(state.hasAcceptedJoin(0) == true);
  CHECK(state.hasAcceptedJoin(1) == true);
  CHECK(state.hasAcceptedJoin(2) == true);
  CHECK(state.hasAcceptedJoin(3) == true);
  CHECK(state.hasAcceptedJoin(4) == false);
}

/** @id TEST-POTTERY-020-001
 * @verifies REQ-POTTERY-020
 */
// acceptCandidateはDES-POTTERY-004の矛盾判定を経て、採用済み候補と矛盾する候補に
// 対してはConflictWarningを返し、AssemblyStateを変更しないこと。
TEST_CASE("TEST-POTTERY-020-001 acceptCandidate returns ConflictWarning without mutating state on conflict") {
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1, 2});

  const auto first = orchestrator.acceptCandidate(
      makeCandidate(0, 1, PropagatedPose{Vec3{10.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 90.0));
  REQUIRE(std::holds_alternative<AssemblyState>(first));

  const auto second = orchestrator.acceptCandidate(
      makeCandidate(1, 2, PropagatedPose{Vec3{10.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 90.0));
  REQUIRE(std::holds_alternative<AssemblyState>(second));
  CHECK(std::get<AssemblyState>(second).acceptedJoins.size() == 2);

  // 0-2の直接候補: 鎖からの伝播姿勢(X+20mm)と矛盾するY軸+10mmの姿勢。
  const auto conflicting = orchestrator.acceptCandidate(
      makeCandidate(0, 2, PropagatedPose{Vec3{0.0, 10.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 80.0));
  REQUIRE(std::holds_alternative<ConflictWarning>(conflicting));
  CHECK(std::get<ConflictWarning>(conflicting).fragmentIdA == 0);
  CHECK(std::get<ConflictWarning>(conflicting).fragmentIdB == 2);

  // 矛盾したacceptCandidate呼び出しはAssemblyStateを変更しない。
  CHECK(orchestrator.state().acceptedJoins.size() == 2);
  CHECK(orchestrator.state().rejectedCandidates.empty());
}

/** @id TEST-POTTERY-007-001
 * @verifies REQ-POTTERY-007
 */
// 半自動モードは候補を信頼度降順（同点は破片ID対昇順）で一覧表示し、
// 利用者の明示コマンドによる採用・却下がAssemblyStateへ反映されること。
TEST_CASE("TEST-POTTERY-007-001 presentCandidates sorts by confidence and explicit accept/reject updates state") {
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1, 2});

  std::vector<JoinCandidate> candidates;
  candidates.push_back(makeCandidate(1, 2, kIdentityPose, 60.0));
  candidates.push_back(makeCandidate(0, 1, kIdentityPose, 95.0));
  candidates.push_back(makeCandidate(0, 2, kIdentityPose, 95.0));

  const CandidateListView view = orchestrator.presentCandidates(candidates);
  REQUIRE(view.candidates.size() == 3);
  // 信頼度95.0の2件は同点のため破片ID対昇順、(0,1)が(0,2)より先。
  CHECK(view.candidates[0].fragmentIdA == 0);
  CHECK(view.candidates[0].fragmentIdB == 1);
  CHECK(view.candidates[1].fragmentIdA == 0);
  CHECK(view.candidates[1].fragmentIdB == 2);
  CHECK(view.candidates[2].fragmentIdA == 1);
  CHECK(view.candidates[2].fragmentIdB == 2);

  const auto accepted = orchestrator.acceptCandidate(candidates[1]);  // (0,1)
  REQUIRE(std::holds_alternative<AssemblyState>(accepted));
  CHECK(std::get<AssemblyState>(accepted).acceptedJoins.size() == 1);

  const AssemblyState& afterReject = orchestrator.rejectCandidate(candidates[0]);  // (1,2)
  REQUIRE(afterReject.rejectedCandidates.size() == 1);
  CHECK(afterReject.rejectedCandidates[0].fragmentIdA == 1);
  CHECK(afterReject.rejectedCandidates[0].fragmentIdB == 2);
  CHECK(afterReject.rejectedCandidates[0].reason == RejectionReason::UserRejected);
}

/** @id TEST-POTTERY-008-001
 * @verifies REQ-POTTERY-008
 */
// 手動微調整による姿勢更新は、接合により伝播された姿勢より優先してAssemblyStateへ
// 反映されること。
TEST_CASE("TEST-POTTERY-008-001 applyManualTransform overrides propagated pose") {
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1});

  orchestrator.acceptCandidate(
      makeCandidate(0, 1, PropagatedPose{Vec3{10.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 90.0));
  REQUIRE(orchestrator.state().resolvedPose(1).has_value());
  CHECK(orchestrator.state().resolvedPose(1)->translationMm.x == doctest::Approx(10.0));

  const PropagatedPose manualPose{Vec3{3.0, 4.0, 5.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
  const AssemblyState& state = orchestrator.applyManualTransform(1, manualPose);

  REQUIRE(state.resolvedPose(1).has_value());
  CHECK(state.resolvedPose(1)->translationMm.x == doctest::Approx(3.0));
  CHECK(state.resolvedPose(1)->translationMm.y == doctest::Approx(4.0));
  CHECK(state.resolvedPose(1)->translationMm.z == doctest::Approx(5.0));
}

/** @id TEST-POTTERY-022-001
 * @verifies REQ-POTTERY-022
 */
// undoは直近の候補採否・姿勢変更操作を取り消してAssemblyStateを直前の状態へ
// 戻すこと。取り消す操作がない場合は何もしないこと。
TEST_CASE("TEST-POTTERY-022-001 undo reverts the most recent operation") {
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1});

  orchestrator.acceptCandidate(
      makeCandidate(0, 1, PropagatedPose{Vec3{10.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 90.0));
  REQUIRE(orchestrator.state().acceptedJoins.size() == 1);

  const AssemblyState& afterUndo = orchestrator.undo();
  CHECK(afterUndo.acceptedJoins.empty());
  CHECK(afterUndo.fragmentIds.size() == 2);

  // 取り消す操作がない場合は何もしない。
  const AssemblyState& afterSecondUndo = orchestrator.undo();
  CHECK(afterSecondUndo.acceptedJoins.empty());
}

/** @id TEST-POTTERY-026-001
 * @verifies REQ-POTTERY-026
 */
// redoはundoで取り消した操作をやり直すこと。やり直す操作がない場合は
// 何もしないこと。
TEST_CASE("TEST-POTTERY-026-001 redo reapplies an undone operation") {
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1});

  orchestrator.acceptCandidate(
      makeCandidate(0, 1, PropagatedPose{Vec3{10.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}}, 90.0));
  orchestrator.undo();
  REQUIRE(orchestrator.state().acceptedJoins.empty());

  const AssemblyState& afterRedo = orchestrator.redo();
  REQUIRE(afterRedo.acceptedJoins.size() == 1);
  CHECK(afterRedo.acceptedJoins[0].fragmentIdB == 1);

  // やり直す操作がない場合は何もしない。
  const AssemblyState& afterSecondRedo = orchestrator.redo();
  CHECK(afterSecondRedo.acceptedJoins.size() == 1);
}
