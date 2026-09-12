#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace kintsugi::core {

/** @id CODE-POTTERY-013
 * @implements REQ-POTTERY-016
 * @design DES-POTTERY-011
 */

// design.md では DES-POTTERY-011（オフライン実行基盤）は「公開APIを持たない
// 横断的制約」と定義されているが、REQ-POTTERY-016（オフライン動作）の
// 「ネットワーク到達性に依存しない」という不在制約を自動テストで裏付ける
// ため、検証専用の最小限のAPIを本ヘッダに追加する（適応）。

// ソースファイル1件中で検出された、ネットワーク到達性への依存を示す
// 既知のシンボル1件（REQ-POTTERY-016違反の疑いがある箇所）。
struct NetworkSymbolFinding {
  std::string filePath;
  std::string symbol;
  std::size_t lineNumber = 0;
};

// sourceFilePaths各ファイルの内容を走査し、ネットワーク呼び出し・ライセンス
// オンライン認証等で用いられる既知のシンボル（curl、socket、connect(、
// http://、https://、boost::asio、winhttp、wininet、getaddrinfo、
// gethostbyname）が含まれていないかを検出する。文字列比較のみの単純な
// 静的走査であり、コメント内の記述と実コードを区別しない（保守的に検出）。
// 何も検出されなければ空のvectorを返す。
std::vector<NetworkSymbolFinding> scanForNetworkDependencySymbols(const std::vector<std::string>& sourceFilePaths);

// sourceRoot配下を再帰的に走査し、拡張子が .hpp または .cpp である全ファイルの
// 絶対パス一覧を返す（走査順は決定的にするためソート済み）。
std::vector<std::string> listCppSourceFiles(const std::string& sourceRoot);

}  // namespace kintsugi::core
