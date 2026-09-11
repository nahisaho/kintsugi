#pragma once

#include <vector>

#include "fragment_mesh.hpp"

namespace kintsugi::core {

/** @id CODE-POTTERY-002
 * @implements REQ-POTTERY-003
 * @design DES-POTTERY-002
 */

// 器物候補として同一グループにまとめられた破片群。fragments[i] の所属信頼度は
// membershipConfidence[i]（0〜100、値が大きいほど所属確度が高い）に対応する。
struct VesselCluster {
  std::vector<FragmentMesh> fragments;
  std::vector<double> membershipConfidence;
};

// clusterFragments の戻り値。vesselCandidates は所属信頼度70以上でグルーピングされた
// 器物候補群、unclassified はどの器物候補にも70以上の信頼度で属さなかった破片群。
struct ClusteringResult {
  std::vector<VesselCluster> vesselCandidates;
  std::vector<FragmentMesh> unclassified;
};

// 複数器物由来の可能性がある破片群を、破片の肉厚統計量と頂点色の類似度に基づく
// 所属信頼度（0〜100）に従い器物候補ごとにグルーピングする（REQ-POTTERY-003）。
// 採用閾値は70に固定し、同一入力に対して常に同一出力を返す決定的処理である。
ClusteringResult clusterFragments(const std::vector<FragmentMesh>& fragments);

}  // namespace kintsugi::core
