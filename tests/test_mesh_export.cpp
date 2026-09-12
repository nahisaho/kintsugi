#include <filesystem>
#include <fstream>

#include "../third_party/doctest/doctest.h"
#include "../src/core/assembly_orchestrator.hpp"
#include "../src/core/fragment_matching.hpp"
#include "../src/core/fragment_mesh.hpp"
#include "../src/core/mesh_export.hpp"
#include "../src/core/pose_conflict.hpp"
#include "../src/core/scan_importer.hpp"

using kintsugi::core::AssemblyOrchestrator;
using kintsugi::core::ExportedFile;
using kintsugi::core::FragmentMesh;
using kintsugi::core::importFragment;
using kintsugi::core::JoinCandidate;
using kintsugi::core::MeshExportFormat;
using kintsugi::core::PropagatedPose;
using kintsugi::core::Quaternion;
using kintsugi::core::Triangle;
using kintsugi::core::Vec3;

namespace {

const PropagatedPose kIdentityPose{Vec3{0.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};

JoinCandidate makeCandidate(std::size_t a, std::size_t b, const PropagatedPose& pose, double confidence) {
  JoinCandidate candidate;
  candidate.fragmentIdA = a;
  candidate.fragmentIdB = b;
  candidate.pose = pose;
  candidate.confidenceScore = confidence;
  return candidate;
}

// 単一の三角形のみからなる最小の合成破片メッシュを生成する。
FragmentMesh makeSingleTriangleFragment(double offsetX) {
  FragmentMesh mesh;
  mesh.vertices = {
      Vec3{offsetX + 0.0, 0.0, 0.0},
      Vec3{offsetX + 1.0, 0.0, 0.0},
      Vec3{offsetX + 0.0, 1.0, 0.0},
  };
  mesh.faces = {Triangle{0, 1, 2}};
  return mesh;
}

// ExportedFileの内容を一時ファイルへ書き出し、そのパスを返す。
std::filesystem::path writeToTempFile(const ExportedFile& file, const std::string& extension) {
  static int counter = 0;
  const auto path = std::filesystem::temp_directory_path() /
                     ("kintsugi_mesh_export_test_" + std::to_string(counter++) + extension);
  std::ofstream out(path, std::ios::binary);
  out << file.content;
  out.close();
  return path;
}

}  // namespace

/** @id TEST-POTTERY-011-001
 * @verifies REQ-POTTERY-011
 */
// 接合関係が確定した破片のみがSTL形式の統合メッシュに含まれ、未接合破片は
// 含まれないこと。また出力ファイルが外部3Dビューア相当（DES-POTTERY-001の
// インポータ）で読み込めること。
TEST_CASE("TEST-POTTERY-011-001 exportIntegratedMesh (STL) includes only confirmed fragments and is externally readable") {
  std::vector<FragmentMesh> fragments{
      makeSingleTriangleFragment(0.0),   // fragment 0: 接合確定。
      makeSingleTriangleFragment(10.0),  // fragment 1: 接合確定。
      makeSingleTriangleFragment(20.0),  // fragment 2: 未接合。
  };

  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1, 2});
  orchestrator.runFullAuto(std::vector<JoinCandidate>{makeCandidate(0, 1, kIdentityPose, 88.0)});

  const auto exported = exportIntegratedMesh(orchestrator.state(), fragments, MeshExportFormat::Stl);
  REQUIRE(exported.format == MeshExportFormat::Stl);
  CHECK(exported.content.find("solid") != std::string::npos);

  const auto tempPath = writeToTempFile(exported, ".stl");
  const auto importResult = importFragment(tempPath.string());
  const auto* reimported = std::get_if<FragmentMesh>(&importResult);
  REQUIRE(reimported != nullptr);

  // fragment 0・1（それぞれ3頂点・1面）のみが含まれ、未接合のfragment 2は
  // 含まれないこと（合計6頂点・2面）。
  CHECK(reimported->vertices.size() == 6);
  CHECK(reimported->faces.size() == 2);

  std::filesystem::remove(tempPath);
}

/** @id TEST-POTTERY-011-002
 * @verifies REQ-POTTERY-011
 */
// OBJ形式でも同様に、接合確定破片のみが統合メッシュに含まれ、外部3Dビューア
// 相当で読み込めること。
TEST_CASE("TEST-POTTERY-011-002 exportIntegratedMesh (OBJ) includes only confirmed fragments and is externally readable") {
  std::vector<FragmentMesh> fragments{
      makeSingleTriangleFragment(0.0),   // fragment 0: 接合確定。
      makeSingleTriangleFragment(10.0),  // fragment 1: 接合確定。
      makeSingleTriangleFragment(20.0),  // fragment 2: 未接合。
  };

  AssemblyOrchestrator orchestrator(std::vector<std::size_t>{0, 1, 2});
  orchestrator.runFullAuto(std::vector<JoinCandidate>{makeCandidate(0, 1, kIdentityPose, 88.0)});

  const auto exported = exportIntegratedMesh(orchestrator.state(), fragments, MeshExportFormat::Obj);
  REQUIRE(exported.format == MeshExportFormat::Obj);
  CHECK(exported.content.find("v ") != std::string::npos);
  CHECK(exported.content.find("f ") != std::string::npos);

  const auto tempPath = writeToTempFile(exported, ".obj");
  const auto importResult = importFragment(tempPath.string());
  const auto* reimported = std::get_if<FragmentMesh>(&importResult);
  REQUIRE(reimported != nullptr);

  CHECK(reimported->vertices.size() == 6);
  CHECK(reimported->faces.size() == 2);

  std::filesystem::remove(tempPath);
}
