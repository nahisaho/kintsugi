#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

#include "fragment_matching.hpp"
#include "pose_conflict.hpp"

namespace kintsugi::core {

/** @id CODE-POTTERY-005
 * @implements REQ-POTTERY-006, REQ-POTTERY-007, REQ-POTTERY-008, REQ-POTTERY-020, REQ-POTTERY-022, REQ-POTTERY-026
 * @design DES-POTTERY-005
 */

// 採用済み接合1件。confidenceScoreは採用元となった接合候補の値をそのまま保持する。
struct AcceptedJoin {
  std::size_t fragmentIdA = 0;
  std::size_t fragmentIdB = 0;
  PropagatedPose pose;
  double confidenceScore = 0.0;
};

// 却下候補1件。完全自動モードでの矛盾却下（REQ-POTTERY-006）、利用者による
// 明示的却下（REQ-POTTERY-007）のいずれで却下されたかをreasonで区別する。
enum class RejectionReason {
  Conflict,
  UserRejected,
};

struct RejectedCandidate {
  std::size_t fragmentIdA = 0;
  std::size_t fragmentIdB = 0;
  double confidenceScore = 0.0;
  RejectionReason reason = RejectionReason::UserRejected;
};

// 採用済み接合1件について、接合成立時点の相対姿勢（join.pose）と、現時点の
// 実際の相対姿勢（双方のresolvedPoseから算出、手動微調整の上書きを反映）との
// 乖離（REQ-POTTERY-008の「隙間・重なり」量）。値が大きいほど接合面に隙間・
// 重なりが生じていることを示す（DES-POTTERY-004と同一の並進差・回転差指標を
// 再利用した近似値であり、符号付きの幾何学的な隙間/重なり距離そのものではない）。
struct GapOverlapEstimate {
  std::size_t neighborFragmentId = 0;
  double translationDeviationMm = 0.0;
  double rotationDeviationDegrees = 0.0;
};

// 器物候補1件分の唯一の権威ある組み立て状態（DES-POTTERY-005）。
struct AssemblyState {
  // 器物候補内の全破片ID一覧（接合候補ゼロの破片を含む）。以後のいかなる
  // 操作でも要素が減少・消失しない。
  std::vector<std::size_t> fragmentIds;
  std::vector<AcceptedJoin> acceptedJoins;
  std::vector<RejectedCandidate> rejectedCandidates;
  // 手動微調整（REQ-POTTERY-008）で明示的に上書きされた破片の基準座標系姿勢。
  // acceptedJoinsから伝播した姿勢より優先して解決に用いる。
  std::unordered_map<std::size_t, PropagatedPose> manualPoseOverrides;

  // fragmentIdをキーとする、当該破片が関与する採用済み接合それぞれについての
  // 隙間・重なり再評価結果（REQ-POTTERY-008）。姿勢変更・接合採否のたびに
  // AssemblyOrchestratorが再計算して最新化する。
  std::unordered_map<std::size_t, std::vector<GapOverlapEstimate>> gapOverlapByFragment;

  // fragmentIdの基準座標系での確立済み推定姿勢を解決する（manualPoseOverrides
  // を最優先し、次にacceptedJoinsからの伝播で求める。未接合ならnullopt）。
  std::optional<PropagatedPose> resolvedPose(std::size_t fragmentId) const;

  // fragmentIdが採用済み接合または手動姿勢設定を1件以上持つか
  // （未接合破片一覧の判定に利用、REQ-POTTERY-009向け）。
  bool hasAcceptedJoin(std::size_t fragmentId) const;
};

// 半自動モード（REQ-POTTERY-007）向けの候補一覧表示用ビュー。信頼度降順・
// 同点は破片ID対昇順で安定的に整列済み。
struct CandidateListView {
  std::vector<JoinCandidate> candidates;
};

// acceptCandidateがDES-POTTERY-004の矛盾判定により候補を採用できなかったことを表す
// （REQ-POTTERY-020：利用者への警告）。
struct ConflictWarning {
  std::size_t fragmentIdA = 0;
  std::size_t fragmentIdB = 0;
  PropagatedPose proposedPose;
  PropagatedPose establishedPose;
};

using AcceptCandidateResult = std::variant<AssemblyState, ConflictWarning>;

// 器物候補1件分の組み立て状態を保持・更新する唯一の書き込み主体（DES-POTTERY-005）。
class AssemblyOrchestrator {
 public:
  explicit AssemblyOrchestrator(std::vector<std::size_t> fragmentIds);

  const AssemblyState& state() const;

  // 完全自動モード（REQ-POTTERY-006）：信頼度70以上の候補を信頼度降順・同点は
  // 破片ID対昇順で走査し、DES-POTTERY-004の基準で矛盾しない候補のみ順次採用、
  // 矛盾候補は却下候補として記録する。
  const AssemblyState& runFullAuto(const std::vector<JoinCandidate>& candidates);

  // 半自動モード（REQ-POTTERY-007）：候補を信頼度順・根拠付きで一覧表示する。
  CandidateListView presentCandidates(const std::vector<JoinCandidate>& candidates) const;

  // 明示コマンドによる候補採用（REQ-POTTERY-007）。採用前にDES-POTTERY-004経由で
  // 採用済み候補との矛盾を判定し（REQ-POTTERY-020）、矛盾する場合はConflictWarningを返す。
  AcceptCandidateResult acceptCandidate(const JoinCandidate& candidate);

  // 明示コマンドによる候補却下（REQ-POTTERY-007）。
  const AssemblyState& rejectCandidate(const JoinCandidate& candidate);

  // 手動微調整（REQ-POTTERY-008）：ドラッグ操作等による姿勢更新コマンドを反映し、
  // 当該破片が関与する採用済み接合の隙間・重なりを再評価してstate().gapOverlapByFragment
  // へ公開する。
  const AssemblyState& applyManualTransform(std::size_t fragmentId, const PropagatedPose& transform);

  // 直近の候補採否・姿勢変更操作を取り消す（REQ-POTTERY-022）。
  const AssemblyState& undo();

  // undoで取り消した操作をやり直す（REQ-POTTERY-026）。
  const AssemblyState& redo();

 private:
  AssemblyState state_;
  std::vector<AssemblyState> undoStack_;
  std::vector<AssemblyState> redoStack_;

  // 変更操作の直前にstate_のスナップショットをundoStack_へ退避し、redoStack_を
  // 破棄する（新規操作により以前のredo系列は無効化される）。
  void snapshotBeforeMutation();

  // state_.acceptedJoins全件について、接合成立時点の相対姿勢と現時点の実際の
  // 相対姿勢（手動微調整の上書きを反映したresolvedPoseから算出）との乖離を
  // 再計算し、state_.gapOverlapByFragmentを最新化する（REQ-POTTERY-008）。
  void recomputeGapOverlap();

  // candidateを、state_の既存確立済み姿勢(anchors)に照らして矛盾判定する。
  // 矛盾する場合はConflictWarningを、矛盾しない場合は伝播後のAcceptedJoinを返す。
  std::variant<AcceptedJoin, ConflictWarning> evaluateCandidate(const JoinCandidate& candidate) const;
};

}  // namespace kintsugi::core
