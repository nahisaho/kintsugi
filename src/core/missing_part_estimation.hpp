#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "assembly_orchestrator.hpp"
#include "fragment_mesh.hpp"

namespace kintsugi::core {

/** @id CODE-POTTERY-006
 * @implements REQ-POTTERY-010
 * @design DES-POTTERY-006
 */

// 推定された欠損部分の概形1件。実測破片データ（FragmentMesh）とは異なる型を
// 用いることで、実測データと区別可能な出力属性を持たせる（REQ-POTTERY-010）。
struct EstimatedShapeRecord {
  std::vector<Vec3> vertices;
  std::vector<Triangle> faces;
};

// assemblyStateとfragmentsから、組み立て済み（採用済み接合または手動配置により
// 姿勢が確立済み）な破片群の全頂点を、resolvedPoseによる基準座標系変換を適用して
// 収集する（未接合の破片は姿勢が未確定のため対象外）。
std::vector<Vec3> collectAssembledVertices(const AssemblyState& assemblyState,
                                            const std::vector<FragmentMesh>& fragments);

// ADR-0005の手法（回転軸推定＋プロファイル曲線の回転）により、組み立て済み破片群
// （assemblyState, fragmentsで指定）から欠損部分の概形を推定する（REQ-POTTERY-010）。
// 回転対称性が検出できない場合はstd::nulloptを返す（optional-feature）。
// design.md上のインターフェースは estimateMissingParts(assemblyState) だが、
// AssemblyStateは破片ID・姿勢のみを保持し実頂点データを持たないため、実頂点取得元
// であるfragmentsを明示的な引数として追加している。
std::optional<std::vector<EstimatedShapeRecord>> estimateMissingParts(
    const AssemblyState& assemblyState, const std::vector<FragmentMesh>& fragments);

}  // namespace kintsugi::core
