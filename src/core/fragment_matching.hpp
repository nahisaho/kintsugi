#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "fragment_mesh.hpp"
#include "pose_conflict.hpp"

namespace kintsugi::core {

/** @id CODE-POTTERY-003
 * @implements REQ-POTTERY-004, REQ-POTTERY-005, REQ-POTTERY-018, REQ-POTTERY-025, REQ-POTTERY-028
 * @design DES-POTTERY-003
 */

// 接合候補の根拠。shapeScore は破断面形状の一致度（0〜100）。colorScore は
// 破片双方に色情報がある場合のみ算出される色・模様連続性スコア（0〜100）。
// icpConverged は破断面点群に対するICPが収束したかどうか（Issue #4:
// 収束状態が信頼度算出へ反映されるようになった。未収束時はshapeScoreが0となる）。
struct JoinEvidence {
  double shapeScore = 0.0;
  std::optional<double> colorScore;
  bool icpConverged = true;
};

// 破片対ごとに算出される唯一の推定接合候補（信頼度最上位の姿勢1件のみ）。
struct JoinCandidate {
  std::size_t fragmentIdA = 0;
  std::size_t fragmentIdB = 0;
  PropagatedPose pose;
  double confidenceScore = 0.0;
  JoinEvidence evidence;
};

// 器物候補内の破片群から、全破片対ごとの接合候補を算出する（REQ-POTTERY-004）。
// ADR-0003の多段階レジストレーションパイプライン（ICPによる姿勢精密化＋
// フィッティング残差ベースの信頼度算出、色情報がある場合は色連続性を加味）に基づく
// 決定的処理であり、破片対の並び順によらず同一の入出力対応を返す。
// 信頼度0〜100によるREQ-POTTERY-006等の採否判定は本関数の呼び出し側が行う
// （本関数自体は閾値によるフィルタリングを行わず、全破片対の候補を返す）。
std::vector<JoinCandidate> computeJoinCandidates(const std::vector<FragmentMesh>& fragments);

}  // namespace kintsugi::core
