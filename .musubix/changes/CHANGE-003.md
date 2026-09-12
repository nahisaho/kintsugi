# CHANGE-003: 破片クラスタリングの推移的併合による所属信頼度70未満混入の修正

Requirements: REQ-POTTERY-003

## 分類
Defect correction（既存要求REQ-POTTERY-003に対する実装の不整合の是正）。
本CHANGEはrequirements.md/design.mdの記述を変更しない。DES-POTTERY-002の
Responsibilitiesは既に「70以上の破片を器物候補ごとにグルーピングし、70未満を
未分類破片として区別する」と規定しており、design自体は正しい。実装
（`src/core/fragment_clustering.cpp`のUnion-Findによる推移的併合）がこの
design/requirementに従っていなかったことが不具合の原因である。

## 経緯
GitHub Issue #3（rubber-duckレビュー指摘）: `clusterFragments`は破片A-B・
B-Cの信頼度がそれぞれ70以上でも、A-C間が70未満の場合にA・B・Cを推移的に
1つのvesselCandidateへ併合してしまい、統合後のmembershipConfidence（グループ
内他破片との平均信頼度）が70未満となる破片を含み得る状態だった。

## 修正内容
Union-Findによる初期候補グループ形成後、各メンバーのグループ内平均信頼度が
70未満になった時点でそのメンバーをグループから除外し、安定するまで反復する
処理を追加した（`src/core/fragment_clustering.cpp`）。除外の結果グループが
1破片以下になった場合は、残った破片も未分類破片として扱う。

## 影響範囲
- `src/core/fragment_clustering.cpp`（実装修正のみ）
- `tests/test_fragment_clustering.cpp`（TEST-POTTERY-003-003を追加）
- requirements.md / design.md: 変更なし（既存の記述のまま）

## テスト
- TEST-POTTERY-003-003（新規、Red→Green確認済み）: A-B・B-Cが70以上、A-Cが
  70未満の推移的併合パターンで、vesselCandidateに含まれる全破片の
  membershipConfidenceが70以上であることを検証する。
- 既存のTEST-POTTERY-003-001/002を含む全35件（新規追加後36件）が
  引き続き成功することを確認する。
