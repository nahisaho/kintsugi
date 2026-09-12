# kintsugi — 土器破片3Dスキャン接合シミュレーション

3Dスキャナーで取得した複数の土器破片データを取り込み、破断面形状の一致度に基づいて
破片同士を自動・半自動でマッチングし、組み立て（接合）結果を3Dビューア上で
確認・微調整・出力できるデスクトップアプリケーションです。

本プロジェクトはSpecification-Driven Development（SDD）ワークフローに基づき、
要求定義（EARS形式）・設計・TDDによる実装・トレーサビリティ検証を経て開発しています。

## 主な機能

- **スキャンデータのインポート**: STL、頂点色・テクスチャ座標付きOBJ（関連MTL含む）、
  法線・RGB色を含みうる点群（XYZ/PCD相当）を読み込み
- **器物クラスタリング**: 複数器物由来の破片群を、クラスタ所属信頼度に基づいて
  器物候補ごとにグルーピング
- **接合候補の推定**: 破断面形状（および色・模様情報があればその連続性）から、
  破片組ごとに信頼度付きの接合候補姿勢を算出
- **3モードの組み立て**:
  - 完全自動モード（信頼度降順で矛盾しない候補を自動採用）
  - 半自動モード（候補一覧から利用者が選択して採否）
  - 手動微調整（3Dビューア上でのドラッグ操作による姿勢調整）
- **矛盾検出・undo/redo**: 姿勢の矛盾判定に基づく警告、操作履歴の取り消し・やり直し
- **欠損部分の推定**（任意機能）: 回転対称形状と判定できる場合に欠損部分の概形を推定
- **3Dビューア**: 回転・ズーム・パンのナビゲーション、破片ホバー時の番号表示
- **レポート・エクスポート**: 未接合破片一覧、却下候補一覧、接合レポート、
  統合メッシュ（STL/OBJ）出力、マッチした破片番号のテキスト出力
- **プロジェクト永続化**: 組み立て状態をZIPコンテナ形式で保存・再読込
- **オフライン動作**: ネットワーク接続なしで全機能が動作

詳しい接合アルゴリズムの解説は [`docs/matching-algorithm.md`](docs/matching-algorithm.md)
を参照してください。

## 技術スタック

- 言語: C++17
- GUI: Qt5 (Core / Gui / Widgets / OpenGL)
- 3D描画: VTK（GUISupportQt / RenderingQt 等）
- 点群・メッシュ処理: PCL (Point Cloud Library) 1.14
- プロジェクトファイル圧縮: minizip
- ハッシュ計算: OpenSSL
- ビルドシステム: CMake (>= 3.16)
- テスト: doctest

## ビルド方法

### 必要な依存関係

- CMake 3.16 以上
- Qt5 開発パッケージ（Core, Gui, Widgets, OpenGL）
- VTK 9系（GUISupportQt / RenderingQt を含むビルド）
- PCL 1.14 以上（common, io, registration, kdtree, search）
- OpenSSL 開発パッケージ
- minizip（pkg-config 経由で検出）

Ubuntu系の例:

```bash
sudo apt install cmake build-essential qtbase5-dev libvtk9-qt-dev \
    libpcl-dev libssl-dev libminizip-dev pkg-config
```

### ビルド手順

```bash
cmake -S . -B build
cmake --build build -j4
```

生成される実行ファイル:

- `build/pottery_app` — デスクトップGUIアプリケーション本体
- `build/pottery_tests` — ユニット/統合テスト（doctest）

### テスト実行

```bash
./build/pottery_tests
```

### アプリケーションの起動

```bash
./build/pottery_app
```

## デモデータ

`demo_data/` に、動作確認用のサンプル破片データ（欠損のある器物破片、
および意図的にマッチしないデコイ破片を含む）を用意しています。詳細は
[`demo_data/README.md`](demo_data/README.md) を参照してください。

## ディレクトリ構成

```
src/
  core/    # インポート・クラスタリング・接合候補推定・組み立て等のコアロジック
  gui/     # Qt/VTKベースのデスクトップGUI
  app/     # エントリーポイント
tests/     # doctestによるユニット/統合テスト
demo_data/ # 動作確認用サンプルデータ
docs/      # アルゴリズム解説等の補足資料
.musubix/  # SDDワークフローの要求定義・設計・トレーサビリティ成果物
```

## ライセンス

[MIT License](LICENSE)
