#pragma once

#include <optional>

#include "../core/assembly_orchestrator.hpp"
#include "../core/fragment_matching.hpp"
#include "../core/pose_conflict.hpp"

namespace kintsugi::gui {

/** @id CODE-POTTERY-010
 * @implements REQ-POTTERY-017
 * @design DES-POTTERY-009
 */

// design.md は「acceptCandidate/rejectCandidate/applyManualTransform/undo/redo
// をDES-POTTERY-005へ委譲するUIコマンドディスパッチ」とのみ記述しており、
// 具体的な型は定義していない。REQ-POTTERY-017の受け入れ基準（Windows実機での
// 起動・操作確認）は本開発環境では検証できないため、実機での動作確認は
// REQ-POTTERY-019と同様に別途GitHub Issueで追跡する。本クラスは、実際のQt
// ウィジェット（ボタン・リスト等）から発火するUIイベントを受け取り、
// DES-POTTERY-005のAssemblyOrchestratorへ委譲する処理が正しく行われることを、
// GUI非依存（ヘッドレス）の形で検証可能にする適応である
// （REQ-POTTERY-007/008のGUI非依存スコープ決定と同じ方針）。
class GuiCommandDispatcher {
 public:
  explicit GuiCommandDispatcher(kintsugi::core::AssemblyOrchestrator& orchestrator);

  // 候補一覧から利用者が採用操作を行った際に呼ばれる。矛盾により採用できな
  // かった場合はlastConflictWarning()で警告を取得できるようにし、状態自体は
  // 変更しない。
  const kintsugi::core::AssemblyState& acceptCandidate(const kintsugi::core::JoinCandidate& candidate);

  // 候補一覧から利用者が却下操作を行った際に呼ばれる。
  const kintsugi::core::AssemblyState& rejectCandidate(const kintsugi::core::JoinCandidate& candidate);

  // 3Dビューア上でのドラッグ操作等による手動微調整が確定した際に呼ばれる
  // （REQ-POTTERY-008）。
  const kintsugi::core::AssemblyState& applyManualDrag(std::size_t fragmentId,
                                                        const kintsugi::core::PropagatedPose& transform);

  // undo/redoメニュー操作。
  const kintsugi::core::AssemblyState& undo();
  const kintsugi::core::AssemblyState& redo();

  // 直近のacceptCandidate呼び出しが矛盾により拒否された場合の警告内容。
  // 矛盾がなかった場合、またはacceptCandidateが一度も矛盾を検出していない
  // 場合はnullopt。
  const std::optional<kintsugi::core::ConflictWarning>& lastConflictWarning() const;

 private:
  kintsugi::core::AssemblyOrchestrator& orchestrator_;
  std::optional<kintsugi::core::ConflictWarning> lastConflictWarning_;
};

}  // namespace kintsugi::gui
