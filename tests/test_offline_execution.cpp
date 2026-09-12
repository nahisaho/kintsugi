#include <cstddef>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "../third_party/doctest/doctest.h"
#include "../src/core/assembly_orchestrator.hpp"
#include "../src/core/fragment_clustering.hpp"
#include "../src/core/fragment_matching.hpp"
#include "../src/core/fragment_mesh.hpp"
#include "../src/core/mesh_export.hpp"
#include "../src/core/missing_part_estimation.hpp"
#include "../src/core/offline_execution.hpp"
#include "../src/core/project_persistence.hpp"
#include "../src/core/scan_importer.hpp"

using kintsugi::core::clusterFragments;
using kintsugi::core::computeFragmentSourceRef;
using kintsugi::core::computeJoinCandidates;
using kintsugi::core::estimateMissingParts;
using kintsugi::core::exportIntegratedMesh;
using kintsugi::core::FragmentMesh;
using kintsugi::core::importFragment;
using kintsugi::core::listCppSourceFiles;
using kintsugi::core::loadProject;
using kintsugi::core::MeshExportFormat;
using kintsugi::core::AssemblyOrchestrator;
using kintsugi::core::ProjectState;
using kintsugi::core::saveProject;
using kintsugi::core::scanForNetworkDependencySymbols;
using kintsugi::core::Triangle;
using kintsugi::core::Vec3;

namespace {

std::filesystem::path uniqueTempPath(const std::string& suffix) {
  static int counter = 0;
  return std::filesystem::temp_directory_path() / ("kintsugi_offline_execution_test_" + std::to_string(counter++) + suffix);
}

// 単一三角形のみからなる最小の合成破片メッシュ（頂点色付き、clusterFragments
// が同一器物候補としてグルーピングできるようにする）。
FragmentMesh makeSingleTriangleFragment(double offsetX) {
  FragmentMesh mesh;
  mesh.vertices = {
      Vec3{offsetX + 0.0, 0.0, 0.0},
      Vec3{offsetX + 1.0, 0.0, 0.0},
      Vec3{offsetX + 0.0, 1.0, 0.0},
  };
  mesh.faces = {Triangle{0, 1, 2}};
  mesh.colors = {{200, 150, 100}, {200, 150, 100}, {200, 150, 100}};
  return mesh;
}

std::filesystem::path writeStlFile(const FragmentMesh& mesh, const std::string& suffix) {
  const auto path = uniqueTempPath(suffix);
  std::ofstream out(path);
  out << "solid fragment\n";
  const auto& v = mesh.vertices;
  out << "facet normal 0 0 1\n outer loop\n";
  out << "  vertex " << v[0].x << " " << v[0].y << " " << v[0].z << "\n";
  out << "  vertex " << v[1].x << " " << v[1].y << " " << v[1].z << "\n";
  out << "  vertex " << v[2].x << " " << v[2].y << " " << v[2].z << "\n";
  out << " endloop\nendfacet\n";
  out << "endsolid fragment\n";
  out.close();
  return path;
}

}  // namespace

/** @id TEST-POTTERY-016-001
 * @verifies REQ-POTTERY-016
 */
// src/配下の全C++ソースファイルに、ネットワーク到達性への依存を示す既知の
// シンボル（curl/socket/connect(/http(s):///boost::asio等）が
// 一切含まれていないこと（オフライン動作の静的裏付け）。
TEST_CASE("TEST-POTTERY-016-001 core/gui source tree contains no known network-dependency symbols") {
  const std::string sourceRoot = std::string(KINTSUGI_SOURCE_DIR) + "/src";
  auto sourceFiles = listCppSourceFiles(sourceRoot);
  REQUIRE(sourceFiles.size() > 10);  // 実ファイルを正しく列挙できていることの健全性チェック。

  // offline_execution.*自体は検出対象シンボルを文字列リテラルとして保持する
  // 検出ロジックそのものであり、実際のネットワーク呼び出しではないため
  // 走査対象から除外する。
  sourceFiles.erase(std::remove_if(sourceFiles.begin(), sourceFiles.end(),
                                    [](const std::string& path) { return path.find("offline_execution") != std::string::npos; }),
                     sourceFiles.end());

  const auto findings = scanForNetworkDependencySymbols(sourceFiles);
  CHECK(findings.empty());
}

/** @id TEST-POTTERY-016-002
 * @verifies REQ-POTTERY-016
 */
// インポートから組み立て結果出力・プロジェクト保存/再読込までの一連の操作が
// 正常に完了すること（REQ-POTTERY-016の受け入れ基準にある一連の操作の実行）。
TEST_CASE("TEST-POTTERY-016-002 full import-to-export-and-persist pipeline completes without error") {
  const auto stlA = writeStlFile(makeSingleTriangleFragment(0.0), ".stl");
  const auto stlB = writeStlFile(makeSingleTriangleFragment(10.0), ".stl");

  // 1) インポート（DES-POTTERY-001）。
  const auto importedA = importFragment(stlA.string());
  const auto importedB = importFragment(stlB.string());
  const auto* meshA = std::get_if<FragmentMesh>(&importedA);
  const auto* meshB = std::get_if<FragmentMesh>(&importedB);
  REQUIRE(meshA != nullptr);
  REQUIRE(meshB != nullptr);
  std::vector<FragmentMesh> fragments{*meshA, *meshB};

  // 2) クラスタリング（DES-POTTERY-002）。
  const auto clustering = clusterFragments(fragments);
  REQUIRE(!clustering.vesselCandidates.empty());

  // 3) 接合推定（DES-POTTERY-003）。
  const auto candidates = computeJoinCandidates(fragments);

  // 4) 組み立て（DES-POTTERY-005、完全自動モード）。
  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1});
  const auto& state = orchestrator.runFullAuto(candidates);

  // 5) 欠損部分推定（DES-POTTERY-006、任意機能。回転対称性が検出できない
  //    合成データのためnulloptとなり得るが、例外なく完了すればよい）。
  (void)estimateMissingParts(state, fragments);

  // 6) 統合メッシュ出力（DES-POTTERY-008）。
  const auto exported = exportIntegratedMesh(state, fragments, MeshExportFormat::Stl);
  CHECK(!exported.content.empty());

  // 7) プロジェクト保存・再読込（DES-POTTERY-010）。
  ProjectState projectState;
  projectState.fragmentSources = {
      computeFragmentSourceRef(0, stlA.string()),
      computeFragmentSourceRef(1, stlB.string()),
  };
  projectState.assemblyStates = {state};
  const auto projectPath = uniqueTempPath(".kintsugiproj");
  saveProject(projectPath.string(), projectState);
  const auto restored = loadProject(projectPath.string());
  CHECK(restored.integrityWarnings.empty());
  REQUIRE(restored.assemblyStates.size() == 1);
  CHECK(restored.assemblyStates[0].fragmentIds == state.fragmentIds);

  // 8) 一連の操作で実際に読み書きした入出力ファイル自体にも、ネットワーク
  //    到達性への依存が持ち込まれていないこと（DES-POTTERY-011）。
  const auto pipelineFileFindings =
      scanForNetworkDependencySymbols({stlA.string(), stlB.string(), projectPath.string()});
  CHECK(pipelineFileFindings.empty());

  std::filesystem::remove(stlA);
  std::filesystem::remove(stlB);
  std::filesystem::remove(projectPath);
}
