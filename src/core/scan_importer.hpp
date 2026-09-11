#pragma once

#include "fragment_mesh.hpp"

namespace kintsugi::core {

/** @id CODE-POTTERY-001
 * @implements REQ-POTTERY-001, REQ-POTTERY-002
 * @design DES-POTTERY-001
 */
// 破片の3Dスキャンデータ（点群/STL/OBJ）を読み込む。
// explicitUnit が指定された場合はその単位から、未指定の場合はミリメートルとして
// 座標をミリメートルへ正規化する。破損・非対応形式・必須ジオメトリ欠如を検出した
// 場合は ImportError を返し、他の読み込み済み破片には一切副作用を及ぼさない。
ImportResult importFragment(const std::string& filePath,
                             std::optional<LengthUnit> explicitUnit = std::nullopt);

// 対応済みフォーマットの一覧を返す。
std::vector<FormatDescriptor> listSupportedFormats();

}  // namespace kintsugi::core
