#pragma once

#include <string>
#include <vector>

#include "assembly_orchestrator.hpp"
#include "fragment_mesh.hpp"

namespace kintsugi::core {

/** @id CODE-POTTERY-008
 * @implements REQ-POTTERY-011
 * @design DES-POTTERY-008
 */

// design.md では STL/OBJ の2値のみを取る文字列引数として定義されているが、
// 誤った文字列の受け渡しを型レベルで防ぐため列挙型として実装する（適応）。
enum class MeshExportFormat {
  Stl,
  Obj,
};

// exportIntegratedMesh の戻り値。書き出し先パスの決定は呼び出し側の責務と
// し、本関数は完成済みのファイル内容（テキスト）のみを返す（design.md の
// ExportedFile を「内容を保持するデータ」として適応した実装）。
struct ExportedFile {
  MeshExportFormat format = MeshExportFormat::Stl;
  std::string content;
};

// AssemblyState上で接合関係が確定した破片（DES-POTTERY-007と同じ
// AssemblyState::hasAcceptedJoin基準で判定、未接合破片は除外）のみを、
// 各破片の確立済み姿勢で変換したうえで単一の三次元メッシュへ統合し、
// STLまたはOBJ形式でエクスポートする（REQ-POTTERY-011）。
// design.mdのインターフェースはAssemblyStateのみを引数に取るが、
// AssemblyStateには生の頂点・面ジオメトリが含まれないため、
// propagatePose/estimateMissingPartsと同様の適応としてfragmentsを
// 明示的な引数に追加する。
ExportedFile exportIntegratedMesh(const AssemblyState& assemblyState, const std::vector<FragmentMesh>& fragments,
                                   MeshExportFormat format);

}  // namespace kintsugi::core
