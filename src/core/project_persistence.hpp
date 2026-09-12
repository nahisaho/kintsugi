#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "assembly_orchestrator.hpp"
#include "missing_part_estimation.hpp"

namespace kintsugi::core {

/** @id CODE-POTTERY-011
 * @implements REQ-POTTERY-015, REQ-POTTERY-021, REQ-POTTERY-023, REQ-POTTERY-024
 * @design DES-POTTERY-010
 */

// インポート済み破片1件分の外部参照情報（生スキャンデータ本体はプロジェクト
// ファイルに埋め込まず、参照パスと内容確認用ハッシュのみを保持する。ADR-0004）。
struct FragmentSourceRef {
  std::size_t fragmentId = 0;
  std::string filePath;
  std::string sha256Hex;
};

// 読込時に検出された参照先スキャンデータの欠落・内容変更（REQ-POTTERY-024）。
enum class IntegrityIssue {
  Missing,
  HashMismatch,
};

struct IntegrityWarning {
  std::size_t fragmentId = 0;
  std::string filePath;
  IntegrityIssue issue = IntegrityIssue::Missing;
};

// プロジェクト状態全体（REQ-POTTERY-015）。design.md は「未分類破片一覧、
// 器物候補ごとのAssemblyState、推定形状レコード」とのみ記述しているため、
// 具体的な保持構造として以下を適応として定義する:
//   - fragmentSources: インポート済み全破片（未分類含む）の外部参照＋ハッシュ
//   - unclassifiedFragmentIds: 未分類破片のID一覧
//   - assemblyStates / estimatedShapesByVessel: 器物候補ごとに並行対応する
//     AssemblyState と推定形状レコード一覧
//   - integrityWarnings: loadProject が検出した破片単位の参照整合性警告
//     （REQ-POTTERY-024）。saveProject 呼び出し時は常に空。
struct ProjectState {
  std::vector<FragmentSourceRef> fragmentSources;
  std::vector<std::size_t> unclassifiedFragmentIds;
  std::vector<AssemblyState> assemblyStates;
  std::vector<std::vector<EstimatedShapeRecord>> estimatedShapesByVessel;
  std::vector<IntegrityWarning> integrityWarnings;
};

// saveProject/loadProject 実行中に内部エラーが発生したことを表す
// （REQ-POTTERY-021）。target は処理対象（ファイルパス等）、reason は原因を示す。
struct PersistenceError : std::runtime_error {
  PersistenceError(std::string target, std::string reason);
  std::string target;
  std::string reason;
};

// fragmentId のインポート元スキャンファイル（filePath）から、外部参照＋
// SHA-256ハッシュ（FragmentSourceRef）を計算する。saveProject 呼び出し前に
// ProjectState::fragmentSources を構築するための補助関数。
FragmentSourceRef computeFragmentSourceRef(std::size_t fragmentId, const std::string& filePath);

// path のプロジェクトファイル（ADR-0004: JSONマニフェスト＋JSON文書群を
// まとめたZIPコンテナ、拡張子 .kintsugiproj）へ projectState を保存する。
// 一時ファイルへ書き込み完了後にアトミックなリネームで既存ファイルを置換する
// （write-temp-then-rename、REQ-POTTERY-023）。リネーム前に内部エラーが
// 発生した場合は PersistenceError を送出し、path に既存のファイルが
// あればそれを一切変更しない。
//
// faultInjectionHookForTesting は一時ファイル書き込み完了後・リネーム前に
// 呼び出される省略可能なテスト専用フックである（本番コードパスでは常に
// nullptr）。REQ-POTTERY-021/023 の「保存処理中に疑似的な内部エラーを
// 発生させる」受け入れ基準を決定的に検証するための適応であり、design.md の
// saveProject(path, projectState) -> void というインターフェースを実質的に
// 変更するものではない（省略時は既定のvoid相当の挙動）。
void saveProject(const std::string& path, const ProjectState& projectState,
                  std::function<void()> faultInjectionHookForTesting = nullptr);

// path のプロジェクトファイルを読み込む。参照先スキャンデータファイルの
// 欠落・内容変更を破片単位で検出し、ProjectState::integrityWarnings に
// 記録した上でそれ以外の状態は正常に復元する（REQ-POTTERY-024）。
// design.md は戻り値を ProjectState | IntegrityWarning[] と記述しているが、
// 参照スキャンデータの欠落・変更は「復元済みProjectStateの一部破片についての
// 警告」であって全体の復元失敗ではないため、常に ProjectState を返し警告は
// その integrityWarnings フィールドに集約する（適応）。プロジェクトファイル
// 自体（ZIPコンテナ・マニフェストJSON）が読み取り・解析不能なほど破損して
// いる場合のみ PersistenceError を送出する。
ProjectState loadProject(const std::string& path);

}  // namespace kintsugi::core
