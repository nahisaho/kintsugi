---
schemaVersion: 1
feature: pottery-fragment-reassembly
---
# Design / 設計: 土器破片3Dスキャン接合シミュレーション

## DES-POTTERY-001: スキャンデータインポーター
Responsibilities: STL、頂点色・テクスチャ座標付きOBJ（関連MTL含む）、法線・RGB色を含みうる生の点群（XYZ/PCD相当）を読み込み、単位情報がなければ既定でミリメートルとみなし、利用者が明示指定した単位があればそれに換算する。破損・非対応形式・必須ジオメトリ欠如を検出した場合、当該ファイルのみをエラー報告し、他の読み込み済み破片は維持する。
Interfaces: importFragment(filePath, explicitUnit?) -> FragmentMesh | ImportError； listSupportedFormats() -> FormatDescriptor[]
Constraints: 1ファイルの読込失敗が他の読込済み破片の状態に影響してはならない（REQ-POTTERY-002）。単位換算規則は既定mm、明示指定優先の1通りのみ。
Requirements: REQ-POTTERY-001, REQ-POTTERY-002
ADRs: ADR-0002
Depends-On: none

## DES-POTTERY-002: 器物クラスタリングエンジン
Responsibilities: 複数器物由来の可能性がある破片群に対し、クラスタ所属信頼度（0〜100）を算出し、70以上の破片を器物候補ごとにグルーピングし、70未満を未分類破片として区別する。
Interfaces: clusterFragments(fragments: FragmentMesh[]) -> {vesselCandidates: VesselCluster[], unclassified: FragmentMesh[]}
Constraints: 採用閾値は70で固定し、PFR-BENCH-CLUSTER-001に対し同一入力から常に同一出力を返す（決定的処理）。
Requirements: REQ-POTTERY-003
ADRs: none — 採用閾値70とグルーピング方式はREQ-POTTERY-003が直接規定しており、代替案間のトレードオフを伴う独立した設計判断は存在しないため。
Depends-On: DES-POTTERY-001

## DES-POTTERY-003: 破断面接合候補推定エンジン
Responsibilities: 器物候補内の破片組ごとに、破断面形状の一致度（ADR-0003の多段階レジストレーションパイプライン）に基づき信頼度最上位の接合候補姿勢1件のみを算出する。破片に色・模様情報がある間は形状類似度に加え色・模様連続性を評価に用いる。信頼度スコアは0〜100とし、値が大きいほど正解接合の可能性が高くなるよう、PFR-BENCH-001上で正解接合ペアの中央値が誤接合ペアの中央値を超えるように較正する。
Interfaces: computeJoinCandidates(vesselCluster: VesselCluster) -> JoinCandidate[]（JoinCandidate: {fragmentIdA, fragmentIdB, pose, confidenceScore, evidence: {shapeScore, colorScore?}}）
Constraints: 同一破片対につき候補は正規化・重複排除後1件のみ。PFR-BENCH-001に対し適合率90%以上（REQ-POTTERY-018）かつ再現率85%以上（REQ-POTTERY-028）。
Requirements: REQ-POTTERY-004, REQ-POTTERY-005, REQ-POTTERY-018, REQ-POTTERY-025, REQ-POTTERY-028
ADRs: ADR-0003
Depends-On: DES-POTTERY-002

## DES-POTTERY-004: 姿勢矛盾判定ユーティリティ
Responsibilities: REQ-POTTERY-027が定義する姿勢矛盾判定基準（同一組み立て済み部分の基準座標系へ伝播した並進差>1.0mm、または回転差>2°）を単一の共有実装として提供し、完全自動組み立て（DES-POTTERY-005）と競合検出・警告（DES-POTTERY-005経由でUIへ）の両方から同一ロジックを参照させる。
Interfaces: propagatePose(candidate: JoinCandidate, assemblyState) -> PropagatedPose； isConflicting(poseA: PropagatedPose, poseB: PropagatedPose) -> boolean
Constraints: 判定式は本コンポーネントに一元化し、REQ-006系（自動採否）とREQ-020（利用者への警告）で判定結果が乖離しないようにする。
Requirements: REQ-POTTERY-027, REQ-POTTERY-020
ADRs: none — 判定式自体はREQ-POTTERY-027が数値基準まで一意に規定しており、代替実装間で選択すべきトレードオフがないため。
Depends-On: DES-POTTERY-003

## DES-POTTERY-005: 組み立てオーケストレーター
Responsibilities: クラスタリング済みの器物候補に属する全破片（接合候補を持たない破片を含む）と算出済み接合候補の全量を単一の権威ある組み立て状態（AssemblyState：全破片の一覧、各破片の採用済み接合・姿勢・未接合状態、却下候補一覧を保持）として保持・更新する唯一の書き込み主体となる。完全自動モード（信頼度70以上の候補を降順・同点は破片ID対昇順で走査し、DES-POTTERY-004の基準で矛盾しない候補のみを順次採用、矛盾候補は却下候補としてAssemblyStateに記録）、半自動モード（候補を信頼度順・根拠付きで一覧表示し、利用者が明示的なコマンドで候補を採否・姿勢選択するとAssemblyStateへ反映）、手動微調整（ドラッグ操作等による姿勢更新コマンドを受け取りAssemblyStateへ反映し隙間・重なりを再評価）の3モードを提供する。候補採否・姿勢変更操作の取り消し（undo）・やり直し（redo）をAssemblyStateの変更履歴として管理し、候補採用前にDES-POTTERY-004経由で採用済み候補との矛盾を判定し利用者に警告する。DES-POTTERY-007・DES-POTTERY-008は本コンポーネントが保持するAssemblyStateのみを参照し、独自に破片・候補の帰属を推測しない。
Interfaces: runFullAuto(assemblyState: AssemblyState) -> AssemblyState； presentCandidates(assemblyState: AssemblyState) -> CandidateListView； acceptCandidate(candidateId, selectedPose) -> AssemblyState | ConflictWarning； rejectCandidate(candidateId) -> AssemblyState； applyManualTransform(fragmentId, transform) -> AssemblyState； undo() -> AssemblyState； redo() -> AssemblyState
Constraints: AssemblyStateは器物候補内の全破片（接合候補ゼロの破片を含む）を初期状態から漏れなく保持し、以後のいかなる操作でも破片が消失しない。PFR-BENCH-HW-001上でPFR-BENCH-100（破片100点）に対しクラスタリング開始から完全自動組み立て結果生成完了まで60分以内（旧REQ-POTTERY-019、本リリースではスコープ外化・requirements.md「対象外」節およびGitHub Issue #1参照）。acceptCandidateはDES-POTTERY-004の矛盾判定を経ずに候補を採用してはならない。
Requirements: REQ-POTTERY-006, REQ-POTTERY-007, REQ-POTTERY-008, REQ-POTTERY-020, REQ-POTTERY-022, REQ-POTTERY-026, REQ-POTTERY-027
ADRs: none — 3モードの挙動と貪欲採用順序は各要求（REQ-006/007/008等）が直接規定しており、接合候補の算出アルゴリズム自体の選定はADR-0003で既に扱っているため、本コンポーネント固有の新たな設計判断は存在しない。
Depends-On: DES-POTTERY-003, DES-POTTERY-004

## DES-POTTERY-006: 欠損部分推定モジュール
Responsibilities: 組み立て済み部分が轆轤成形による回転対称形状と判定でき、本機能が有効な場合に、ADR-0005の手法（回転軸推定＋プロファイル曲線の回転）により欠損部分の概形を推定し、実測破片データとは区別可能な独立の推定形状レコードとして出力する。
Interfaces: estimateMissingParts(assemblyState: AssemblyState) -> EstimatedShapeRecord[] | null
Constraints: 回転対称と判定できない場合、または機能無効時は推定を行わない（optional-feature、REQ-POTTERY-010）。
Requirements: REQ-POTTERY-010
ADRs: ADR-0005
Depends-On: DES-POTTERY-005

## DES-POTTERY-007: 未接合破片・却下候補・レポート集計
Responsibilities: DES-POTTERY-005が器物候補ごとに保持する各AssemblyState（器物候補内の全破片一覧を含む）と、DES-POTTERY-002が出力する未分類破片一覧の双方を参照し、採用済み接合を持たない破片（器物候補内の未接合破片、および未分類破片の全件）を未接合破片一覧として集計し既定の統合メッシュ出力対象から除外する。完全自動モードでAssemblyStateに記録された却下候補を却下候補一覧として区別して保持・出力する。接合箇所一覧・各接合の一致度スコアを含むレポート、およびインポート済み全破片（各器物候補のAssemblyState内の破片＋未分類破片、未接合分を含む）の識別子・変換行列・接合状態（確定済み／未接合）を含む出力データを生成する。
Interfaces: buildUnmatchedFragmentReport(assemblyStates: AssemblyState[], unclassified: FragmentMesh[]) -> UnmatchedReport； buildRejectedCandidateReport(assemblyStates: AssemblyState[]) -> RejectedCandidateReport； buildJoinReport(assemblyStates: AssemblyState[]) -> JoinReport； buildPoseExport(assemblyStates: AssemblyState[], unclassified: FragmentMesh[]) -> PoseExportData
Constraints: 未接合破片一覧（REQ-009）と却下候補一覧（REQ-006）は別概念として明確に区別し、混同して出力しない。PoseExportDataはインポート済み全破片（各AssemblyState内の破片＋未分類破片）を漏れなく含み、未分類破片は既定で未接合状態として扱う。
Requirements: REQ-POTTERY-009, REQ-POTTERY-012, REQ-POTTERY-013
ADRs: none — DES-POTTERY-005・DES-POTTERY-002が保持する状態を集計・整形するのみで、独立した設計上のトレードオフを伴わないため。
Depends-On: DES-POTTERY-005, DES-POTTERY-002

## DES-POTTERY-008: 統合メッシュエクスポーター
Responsibilities: 利用者が組み立て結果の確定を指示したとき、DES-POTTERY-005が保持するAssemblyStateのうち接合関係が確定した破片のみを統合した三次元メッシュをSTLまたはOBJ形式でエクスポートする。
Interfaces: exportIntegratedMesh(assemblyState: AssemblyState, format: "stl"|"obj") -> ExportedFile
Constraints: AssemblyState上で未接合と判定された破片（DES-POTTERY-007のUnmatchedReport対象）は統合メッシュに含めない。
Requirements: REQ-POTTERY-011
ADRs: none — STL/OBJの標準的な書き出し処理であり、競合する設計代替案を伴わないため。
Depends-On: DES-POTTERY-005, DES-POTTERY-007

## DES-POTTERY-009: 3Dビューア・デスクトップGUI
Responsibilities: 組み立て結果を3Dビューア上に表示し、回転・拡大縮小・移動の視点操作を提供する。Windowsデスクトップ環境で動作するGUIアプリケーションとして提供し、候補一覧表示・ドラッグによる手動編集・矛盾警告・undo/redo操作のユーザーインターフェースを提供する。
Interfaces: renderScene(assemblyState: AssemblyState) -> void； viewport操作（回転・ズーム・パン）のUIイベントハンドラ；acceptCandidate/rejectCandidate/applyManualTransform/undo/redoをDES-POTTERY-005へ委譲するUIコマンドディスパッチ
Constraints: GUIはWindowsデスクトップ単体で動作し、ネットワーク接続を前提としない（DES-POTTERY-011と整合）。
Requirements: REQ-POTTERY-014, REQ-POTTERY-017
ADRs: ADR-0002
Depends-On: DES-POTTERY-005

## DES-POTTERY-010: プロジェクト永続化マネージャー
Responsibilities: DES-POTTERY-002が出力する未分類破片一覧、DES-POTTERY-005が器物候補ごとに保持する各AssemblyState、およびDES-POTTERY-006の推定形状レコードを含むプロジェクト状態（ProjectState）を、スキャンデータへの参照＋内容確認用ハッシュとともにADR-0004のZIPコンテナ形式で保存・再読込する。保存はwrite-temp-then-rename方式により原子的に行い失敗時に既存ファイルを破損させない。読込時に参照先スキャンデータの欠落・内容変更を破片単位で検出・報告する。解析処理・保存処理中の内部エラー発生時は処理対象と原因を報告し、直前に保存されたプロジェクト状態を破損させずに操作可能な状態へ復帰する。
Interfaces: saveProject(path, projectState: ProjectState) -> void； loadProject(path) -> ProjectState | IntegrityWarning[]
Constraints: 生スキャンデータ本体はコンテナに埋め込まず外部参照＋ハッシュのみ保持する（ADR-0004）。ProjectStateは未分類破片を含むインポート済み全破片を漏れなく保持する。
Requirements: REQ-POTTERY-015, REQ-POTTERY-021, REQ-POTTERY-023, REQ-POTTERY-024
ADRs: ADR-0004
Depends-On: DES-POTTERY-001, DES-POTTERY-002, DES-POTTERY-005

## DES-POTTERY-011: オフライン実行基盤
Responsibilities: インポートから組み立て結果出力までの全機能が、ネットワーク接続がない状態でも実行可能であることを保証する横断的基盤（外部API呼び出し・ライセンスオンライン認証等をコア機能の実行経路に含めない）。
Interfaces: none（横断的制約であり公開APIを持たない）
Constraints: コア機能（DES-POTTERY-001〜010）はいずれもネットワーク到達性に依存してはならない。
Requirements: REQ-POTTERY-016
ADRs: none — ネットワーク呼び出しを行わないという不在制約であり、比較対象となる設計代替案が存在しないため。
Depends-On: none
