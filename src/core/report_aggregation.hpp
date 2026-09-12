#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "assembly_orchestrator.hpp"
#include "fragment_mesh.hpp"
#include "pose_conflict.hpp"

namespace kintsugi::core {

/** @id CODE-POTTERY-007
 * @implements REQ-POTTERY-009, REQ-POTTERY-012, REQ-POTTERY-013
 * @design DES-POTTERY-007
 */

// design.md のインターフェースは assemblyStates: AssemblyState[] のみを引数に
// 取り、破片ジオメトリそのものは扱わない。器物候補内破片・未分類破片を横断して
// 一意に特定するため、design.md からの明示的な適応として FragmentReference
// （clusterIndex + fragmentId）を導入する。clusterIndex は呼び出し側が渡す
// assemblyStates（各器物候補のAssemblyState）内でのインデックスであり、
// clusterIndex が nullopt の場合は未分類破片（unclassified配列内のインデックス
// が fragmentId）を指す。DES-POTTERY-002のクラスタリング結果とDES-POTTERY-005の
// AssemblyState群を対応付ける責務は呼び出し側にある（本コンポーネントは両者の
// 対応順序を独自に推測しない、design.mdのDES-POTTERY-007 Constraints準拠）。
struct FragmentReference {
  std::optional<std::size_t> clusterIndex;
  std::size_t fragmentId = 0;
};

// 未接合破片一覧（REQ-POTTERY-009）。器物候補内で採用済み接合を持たない破片、
// および未分類破片（DES-POTTERY-002のunclassified）の全件を含む。却下候補
// （REQ-POTTERY-006）とは別概念であり、本レポートには含まれない。
struct UnmatchedReport {
  std::vector<FragmentReference> unmatchedFragments;
};

// 却下候補一覧。完全自動モードでの矛盾却下・利用者による明示的却下の別を保持する。
struct RejectedCandidateEntry {
  std::optional<std::size_t> clusterIndex;
  std::size_t fragmentIdA = 0;
  std::size_t fragmentIdB = 0;
  double confidenceScore = 0.0;
  RejectionReason reason = RejectionReason::UserRejected;
};

struct RejectedCandidateReport {
  std::vector<RejectedCandidateEntry> rejectedCandidates;
};

// 接合箇所一覧・一致度スコアレポート（REQ-POTTERY-013）。採用済み接合（AcceptedJoin）
// のみを対象とする。
struct JoinReportEntry {
  std::optional<std::size_t> clusterIndex;
  std::size_t fragmentIdA = 0;
  std::size_t fragmentIdB = 0;
  double confidenceScore = 0.0;
};

struct JoinReport {
  std::vector<JoinReportEntry> joins;
};

// 破片配置・姿勢データ出力（REQ-POTTERY-012）1件分。joinState は確定済み／
// 未接合のいずれか。未接合破片の pose は恒等姿勢（並進・回転なし）とする
// （実際の姿勢は未確立のため、位置情報としての意味を持たない旨は出力属性
// joinState=Unmatched で判別する）。
enum class JoinState {
  Confirmed,
  Unmatched,
};

struct PoseExportEntry {
  FragmentReference fragment;
  PropagatedPose pose;
  JoinState joinState = JoinState::Unmatched;
};

struct PoseExportData {
  std::vector<PoseExportEntry> fragments;
};

// 器物候補ごとのAssemblyStateと未分類破片一覧から、採用済み接合を持たない
// 破片（器物候補内の未接合破片＋未分類破片の全件）を集計する（REQ-POTTERY-009）。
UnmatchedReport buildUnmatchedFragmentReport(const std::vector<AssemblyState>& assemblyStates,
                                              const std::vector<FragmentMesh>& unclassified);

// 各AssemblyStateが保持する却下候補一覧を集計する。
RejectedCandidateReport buildRejectedCandidateReport(const std::vector<AssemblyState>& assemblyStates);

// 各AssemblyStateが保持する採用済み接合一覧から、接合箇所・一致度スコアレポートを
// 生成する（REQ-POTTERY-013）。
JoinReport buildJoinReport(const std::vector<AssemblyState>& assemblyStates);

// インポート済み全破片（各AssemblyState内の破片＋未分類破片）の識別子・変換行列・
// 接合状態を出力する（REQ-POTTERY-012）。未分類破片は既定で未接合状態として扱う。
PoseExportData buildPoseExport(const std::vector<AssemblyState>& assemblyStates,
                                const std::vector<FragmentMesh>& unclassified);

}  // namespace kintsugi::core
