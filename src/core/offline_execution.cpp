#include "offline_execution.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace kintsugi::core {

namespace {

// 検出対象の既知シンボル一覧（REQ-POTTERY-016：ネットワーク呼び出し・
// ライセンスオンライン認証等でよく用いられるAPI/プロトコル識別子）。
const std::vector<std::string>& forbiddenNetworkSymbols() {
  static const std::vector<std::string> symbols = {
      "curl",     "socket(",  "connect(",     "http://",      "https://",
      "boost::asio", "winhttp", "wininet", "getaddrinfo", "gethostbyname",
  };
  return symbols;
}

}  // namespace

std::vector<NetworkSymbolFinding> scanForNetworkDependencySymbols(const std::vector<std::string>& sourceFilePaths) {
  std::vector<NetworkSymbolFinding> findings;
  for (const auto& filePath : sourceFilePaths) {
    std::ifstream in(filePath);
    if (!in) continue;  // 読めないファイルはスキップ（呼び出し元の責務外）。

    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(in, line)) {
      ++lineNumber;
      for (const auto& symbol : forbiddenNetworkSymbols()) {
        if (line.find(symbol) != std::string::npos) {
          findings.push_back(NetworkSymbolFinding{filePath, symbol, lineNumber});
        }
      }
    }
  }
  return findings;
}

std::vector<std::string> listCppSourceFiles(const std::string& sourceRoot) {
  std::vector<std::string> files;
  if (!std::filesystem::exists(sourceRoot)) return files;

  for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot)) {
    if (!entry.is_regular_file()) continue;
    const auto extension = entry.path().extension().string();
    if (extension == ".hpp" || extension == ".cpp") {
      files.push_back(std::filesystem::absolute(entry.path()).string());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

}  // namespace kintsugi::core
