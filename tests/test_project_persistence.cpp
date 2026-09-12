#include <cstdio>
#include <filesystem>
#include <fstream>

#include "../third_party/doctest/doctest.h"
#include "../src/core/assembly_orchestrator.hpp"
#include "../src/core/missing_part_estimation.hpp"
#include "../src/core/pose_conflict.hpp"
#include "../src/core/project_persistence.hpp"

using kintsugi::core::AcceptedJoin;
using kintsugi::core::AssemblyState;
using kintsugi::core::computeFragmentSourceRef;
using kintsugi::core::EstimatedShapeRecord;
using kintsugi::core::FragmentSourceRef;
using kintsugi::core::IntegrityIssue;
using kintsugi::core::loadProject;
using kintsugi::core::PersistenceError;
using kintsugi::core::ProjectState;
using kintsugi::core::PropagatedPose;
using kintsugi::core::Quaternion;
using kintsugi::core::RejectedCandidate;
using kintsugi::core::RejectionReason;
using kintsugi::core::saveProject;
using kintsugi::core::Vec3;

namespace {

// テスト用の一時ディレクトリ配下に固有のパスを払い出す。
std::filesystem::path uniqueTempPath(const std::string& suffix) {
  static int counter = 0;
  return std::filesystem::temp_directory_path() /
         ("kintsugi_project_persistence_test_" + std::to_string(counter++) + suffix);
}

// スキャンデータ参照先として使う実ファイルを作成し、パスを返す。
std::filesystem::path writeScanDataFile(const std::string& content) {
  const auto path = uniqueTempPath(".scan");
  std::ofstream out(path, std::ios::binary);
  out << content;
  out.close();
  return path;
}

// クラスタリング＋組み立て済みの器物候補1件・未分類破片1件を含む、内容の
// ある ProjectState を組み立てる（往復確認用）。
ProjectState buildSampleProjectState(const std::filesystem::path& scanA, const std::filesystem::path& scanB,
                                      const std::filesystem::path& scanUnclassified) {
  ProjectState state;
  state.fragmentSources = {
      computeFragmentSourceRef(0, scanA.string()),
      computeFragmentSourceRef(1, scanB.string()),
      computeFragmentSourceRef(2, scanUnclassified.string()),
  };
  state.unclassifiedFragmentIds = {2};

  AssemblyState vessel;
  vessel.fragmentIds = {0, 1};
  vessel.acceptedJoins.push_back(AcceptedJoin{
      0, 1, PropagatedPose{Vec3{1.5, 2.5, 3.5}, Quaternion{0.9, 0.1, 0.2, 0.3}}, 92.0});
  vessel.rejectedCandidates.push_back(RejectedCandidate{0, 1, 40.0, RejectionReason::UserRejected});
  vessel.manualPoseOverrides[1] = PropagatedPose{Vec3{9.0, 8.0, 7.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
  state.assemblyStates = {vessel};

  EstimatedShapeRecord shape;
  shape.vertices = {Vec3{0.0, 0.0, 0.0}, Vec3{1.0, 0.0, 0.0}, Vec3{0.0, 1.0, 0.0}};
  shape.faces = {{0, 1, 2}};
  state.estimatedShapesByVessel = {{shape}};

  return state;
}

}  // namespace

/** @id TEST-POTTERY-015-001
 * @verifies REQ-POTTERY-015
 */
// プロジェクトを保存し読み込んだ際、配置・手動編集内容・クラスタリング/接合
// 推定結果・参照先スキャンデータのハッシュを含む状態が復元されること。
TEST_CASE("TEST-POTTERY-015-001 saveProject/loadProject round-trip restores full project state") {
  const auto scanA = writeScanDataFile("fragment-a-content");
  const auto scanB = writeScanDataFile("fragment-b-content");
  const auto scanUnclassified = writeScanDataFile("fragment-unclassified-content");
  const auto projectPath = uniqueTempPath(".kintsugiproj");

  const ProjectState original = buildSampleProjectState(scanA, scanB, scanUnclassified);
  saveProject(projectPath.string(), original);

  const ProjectState restored = loadProject(projectPath.string());

  CHECK(restored.integrityWarnings.empty());

  REQUIRE(restored.fragmentSources.size() == 3);
  CHECK(restored.fragmentSources[0].fragmentId == 0);
  CHECK(restored.fragmentSources[0].filePath == scanA.string());
  CHECK(restored.fragmentSources[0].sha256Hex == original.fragmentSources[0].sha256Hex);
  CHECK(!restored.fragmentSources[0].sha256Hex.empty());

  REQUIRE(restored.unclassifiedFragmentIds.size() == 1);
  CHECK(restored.unclassifiedFragmentIds[0] == 2);

  REQUIRE(restored.assemblyStates.size() == 1);
  const auto& vessel = restored.assemblyStates[0];
  CHECK(vessel.fragmentIds == std::vector<std::size_t>{0, 1});
  REQUIRE(vessel.acceptedJoins.size() == 1);
  CHECK(vessel.acceptedJoins[0].fragmentIdA == 0);
  CHECK(vessel.acceptedJoins[0].fragmentIdB == 1);
  CHECK(vessel.acceptedJoins[0].confidenceScore == doctest::Approx(92.0));
  CHECK(vessel.acceptedJoins[0].pose.translationMm.x == doctest::Approx(1.5));
  REQUIRE(vessel.rejectedCandidates.size() == 1);
  CHECK(vessel.rejectedCandidates[0].reason == RejectionReason::UserRejected);
  REQUIRE(vessel.manualPoseOverrides.count(1) == 1);
  CHECK(vessel.manualPoseOverrides.at(1).translationMm.x == doctest::Approx(9.0));

  REQUIRE(restored.estimatedShapesByVessel.size() == 1);
  REQUIRE(restored.estimatedShapesByVessel[0].size() == 1);
  CHECK(restored.estimatedShapesByVessel[0][0].vertices.size() == 3);
  CHECK(restored.estimatedShapesByVessel[0][0].faces.size() == 1);

  std::filesystem::remove(projectPath);
  std::filesystem::remove(scanA);
  std::filesystem::remove(scanB);
  std::filesystem::remove(scanUnclassified);
}

/** @id TEST-POTTERY-024-001
 * @verifies REQ-POTTERY-024
 */
// 参照先スキャンデータファイルが削除・改変された状態でプロジェクトを読み込むと、
// 当該破片が識別可能な形（fragmentId・filePath・種別）で報告されること。
TEST_CASE("TEST-POTTERY-024-001 loadProject reports missing and modified referenced scan data per fragment") {
  const auto scanMissing = writeScanDataFile("will-be-deleted");
  const auto scanModified = writeScanDataFile("original-content");
  const auto scanUntouched = writeScanDataFile("untouched-content");
  const auto projectPath = uniqueTempPath(".kintsugiproj");

  ProjectState state;
  state.fragmentSources = {
      computeFragmentSourceRef(10, scanMissing.string()),
      computeFragmentSourceRef(11, scanModified.string()),
      computeFragmentSourceRef(12, scanUntouched.string()),
  };
  state.unclassifiedFragmentIds = {10, 11, 12};
  saveProject(projectPath.string(), state);

  // 保存後に参照先スキャンデータを破壊する。
  std::filesystem::remove(scanMissing);
  {
    std::ofstream out(scanModified, std::ios::binary | std::ios::trunc);
    out << "tampered-content";
  }

  const ProjectState restored = loadProject(projectPath.string());

  REQUIRE(restored.integrityWarnings.size() == 2);
  const auto& missingWarning = restored.integrityWarnings[0];
  CHECK(missingWarning.fragmentId == 10);
  CHECK(missingWarning.filePath == scanMissing.string());
  CHECK(missingWarning.issue == IntegrityIssue::Missing);

  const auto& modifiedWarning = restored.integrityWarnings[1];
  CHECK(modifiedWarning.fragmentId == 11);
  CHECK(modifiedWarning.filePath == scanModified.string());
  CHECK(modifiedWarning.issue == IntegrityIssue::HashMismatch);

  std::filesystem::remove(projectPath);
  std::filesystem::remove(scanModified);
  std::filesystem::remove(scanUntouched);
}

/** @id TEST-POTTERY-023-001
 * @verifies REQ-POTTERY-023
 */
// 保存処理中に強制的に中断させても、直前に保存されていたプロジェクトファイルが
// 読み込み可能な状態で維持されること（write-temp-then-renameによる原子性）。
TEST_CASE("TEST-POTTERY-023-001 interrupted save preserves previously saved project file") {
  const auto scanA = writeScanDataFile("fragment-a-content");
  const auto projectPath = uniqueTempPath(".kintsugiproj");

  ProjectState firstState;
  firstState.fragmentSources = {computeFragmentSourceRef(0, scanA.string())};
  firstState.unclassifiedFragmentIds = {0};
  saveProject(projectPath.string(), firstState);

  ProjectState secondState;
  secondState.fragmentSources = {computeFragmentSourceRef(0, scanA.string())};
  secondState.assemblyStates.push_back(AssemblyState{});
  secondState.assemblyStates[0].fragmentIds = {0};

  bool threw = false;
  try {
    saveProject(projectPath.string(), secondState, [] { throw std::runtime_error("simulated mid-save failure"); });
  } catch (const std::exception&) {
    threw = true;
  }
  CHECK(threw);

  // 中断前の状態（未分類破片1件・器物候補0件）がそのまま読み込めること。
  const ProjectState restored = loadProject(projectPath.string());
  CHECK(restored.unclassifiedFragmentIds == std::vector<std::size_t>{0});
  CHECK(restored.assemblyStates.empty());

  std::filesystem::remove(projectPath);
  std::filesystem::remove(scanA);
}

/** @id TEST-POTTERY-021-001
 * @verifies REQ-POTTERY-021
 */
// 保存処理中に内部エラーが発生した場合、処理対象（パス）と原因を含む例外が
// 送出され、既存の直前保存済みプロジェクトファイルは破損しないこと。
TEST_CASE("TEST-POTTERY-021-001 saveProject reports target and reason on internal error without corrupting prior save") {
  const auto scanA = writeScanDataFile("fragment-a-content");
  const auto projectPath = uniqueTempPath(".kintsugiproj");

  ProjectState firstState;
  firstState.fragmentSources = {computeFragmentSourceRef(0, scanA.string())};
  firstState.unclassifiedFragmentIds = {0};
  saveProject(projectPath.string(), firstState);

  ProjectState secondState;
  secondState.fragmentSources = {computeFragmentSourceRef(0, scanA.string())};
  secondState.unclassifiedFragmentIds = {0, 99};  // 内容を変えて後続比較を意味あるものにする。

  bool caughtPersistenceError = false;
  try {
    saveProject(projectPath.string(), secondState, [&projectPath] {
      throw PersistenceError(projectPath.string(), "疑似的な内部エラー（解析処理中の異常を模擬）");
    });
  } catch (const PersistenceError& e) {
    caughtPersistenceError = true;
    CHECK(e.target == projectPath.string());
    CHECK(!e.reason.empty());
  }
  CHECK(caughtPersistenceError);

  const ProjectState restored = loadProject(projectPath.string());
  CHECK(restored.unclassifiedFragmentIds == std::vector<std::size_t>{0});

  std::filesystem::remove(projectPath);
  std::filesystem::remove(scanA);
}

/** @id TEST-POTTERY-021-002
 * @verifies REQ-POTTERY-021
 */
// 存在しないディレクトリへの保存など、リネーム以前の書き込み段階で失敗する
// 内部エラーについても、対象パスと原因を含む例外が送出されること。
TEST_CASE("TEST-POTTERY-021-002 saveProject reports target and reason when temp file cannot be created") {
  const auto scanA = writeScanDataFile("fragment-a-content");
  const auto invalidPath =
      (std::filesystem::temp_directory_path() / "kintsugi_nonexistent_dir_for_test" / "project.kintsugiproj").string();

  ProjectState state;
  state.fragmentSources = {computeFragmentSourceRef(0, scanA.string())};
  state.unclassifiedFragmentIds = {0};

  bool caughtPersistenceError = false;
  try {
    saveProject(invalidPath, state);
  } catch (const PersistenceError& e) {
    caughtPersistenceError = true;
    CHECK(e.target == invalidPath);
    CHECK(!e.reason.empty());
  }
  CHECK(caughtPersistenceError);

  std::filesystem::remove(scanA);
}
