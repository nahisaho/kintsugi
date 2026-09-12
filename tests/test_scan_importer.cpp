#include "../third_party/doctest/doctest.h"
#include "../src/core/scan_importer.hpp"

#include <cmath>

using kintsugi::core::ColorRGB;
using kintsugi::core::FragmentMesh;
using kintsugi::core::ImportError;
using kintsugi::core::ImportResult;
using kintsugi::core::LengthUnit;
using kintsugi::core::importFragment;

namespace {
constexpr const char* kFixtureWithNormalsColors =
    "tests/fixtures/pointcloud_with_normals_colors.pcd";
constexpr const char* kFixtureCorrupted = "tests/fixtures/corrupted.pcd";
constexpr const char* kFixtureXyzOnly = "tests/fixtures/pointcloud_xyz_only.pcd";
constexpr double kEpsilon = 1e-6;
}  // namespace

/** @id TEST-POTTERY-001-001
 * @verifies REQ-POTTERY-001
 */
TEST_CASE("TEST-POTTERY-001-001: 単位情報なしの点群は既定でミリメートルとして、法線・色を保持したまま読み込まれる") {
  ImportResult result = importFragment(kFixtureWithNormalsColors);

  REQUIRE(std::holds_alternative<FragmentMesh>(result));
  const FragmentMesh& mesh = std::get<FragmentMesh>(result);

  REQUIRE(mesh.vertices.size() == 3);
  CHECK(mesh.vertices[0].x == doctest::Approx(10.0).epsilon(kEpsilon));
  CHECK(mesh.vertices[0].y == doctest::Approx(20.0).epsilon(kEpsilon));
  CHECK(mesh.vertices[0].z == doctest::Approx(30.0).epsilon(kEpsilon));
  CHECK(mesh.vertices[2].x == doctest::Approx(-5.5).epsilon(kEpsilon));

  REQUIRE(mesh.normals.size() == 3);
  CHECK(mesh.normals[0].z == doctest::Approx(1.0).epsilon(kEpsilon));
  CHECK(mesh.normals[2].x == doctest::Approx(1.0).epsilon(kEpsilon));

  REQUIRE(mesh.colors.size() == 3);
  CHECK(mesh.colors[0].r == 255);
  CHECK(mesh.colors[0].g == 0);
  CHECK(mesh.colors[1].g == 255);
  CHECK(mesh.colors[2].b == 255);
}

/** @id TEST-POTTERY-002-001
 * @verifies REQ-POTTERY-002
 */
TEST_CASE(
    "TEST-POTTERY-002-001: 破損・非対応形式・ジオメトリ欠如の各ファイルはエラー報告され、既に読み込み済みの他破片には影響しない") {
  // 事前に1件、正常な破片を読み込んでおく。
  ImportResult firstOk = importFragment(kFixtureXyzOnly);
  REQUIRE(std::holds_alternative<FragmentMesh>(firstOk));
  const std::size_t firstVertexCount = std::get<FragmentMesh>(firstOk).vertices.size();

  // 破損ファイル: エラーとして報告される。
  ImportResult corrupted = importFragment(kFixtureCorrupted);
  REQUIRE(std::holds_alternative<ImportError>(corrupted));

  // 非対応拡張子: エラーとして報告される。
  ImportResult unsupported = importFragment("tests/fixtures/unsupported.xyz");
  REQUIRE(std::holds_alternative<ImportError>(unsupported));

  // ジオメトリ情報（x/y/z）を欠くファイル: エラーとして報告される。
  ImportResult missingGeometry = importFragment("tests/fixtures/missing_geometry.pcd");
  REQUIRE(std::holds_alternative<ImportError>(missingGeometry));

  // 直前のエラー読込群の後でも、既存の正常破片は影響を受けずに再読込できる。
  ImportResult secondOk = importFragment(kFixtureXyzOnly);
  REQUIRE(std::holds_alternative<FragmentMesh>(secondOk));
  CHECK(std::get<FragmentMesh>(secondOk).vertices.size() == firstVertexCount);
}

/** @id TEST-POTTERY-001-002
 * @verifies REQ-POTTERY-001
 */
TEST_CASE("TEST-POTTERY-001-002: 明示的にセンチメートル単位を指定した場合、座標がミリメートルへ換算されて読み込まれる") {
  ImportResult result = importFragment(kFixtureXyzOnly, LengthUnit::Centimeter);

  REQUIRE(std::holds_alternative<FragmentMesh>(result));
  const FragmentMesh& mesh = std::get<FragmentMesh>(result);

  REQUIRE(mesh.vertices.size() == 2);
  CHECK(mesh.vertices[0].x == doctest::Approx(10.0).epsilon(kEpsilon));
  CHECK(mesh.vertices[0].y == doctest::Approx(20.0).epsilon(kEpsilon));
  CHECK(mesh.vertices[0].z == doctest::Approx(30.0).epsilon(kEpsilon));
  CHECK(mesh.vertices[1].x == doctest::Approx(40.0).epsilon(kEpsilon));
}

/** @id TEST-POTTERY-001-003
 * @verifies REQ-POTTERY-001
 */
TEST_CASE("TEST-POTTERY-001-003: STL形式のメッシュは頂点座標と三角面が既定ミリメートルとして読み込まれる") {
  ImportResult result = importFragment("tests/fixtures/quad_two_triangles.stl");

  REQUIRE(std::holds_alternative<FragmentMesh>(result));
  const FragmentMesh& mesh = std::get<FragmentMesh>(result);

  REQUIRE(mesh.vertices.size() == 4);
  CHECK(mesh.vertices[0].x == doctest::Approx(0.0).epsilon(kEpsilon));
  CHECK(mesh.vertices[1].x == doctest::Approx(10.0).epsilon(kEpsilon));
  CHECK(mesh.vertices[2].y == doctest::Approx(10.0).epsilon(kEpsilon));

  REQUIRE(mesh.faces.size() == 2);
  CHECK(mesh.faces[0].v0 == 0);
  CHECK(mesh.faces[0].v1 == 1);
  CHECK(mesh.faces[0].v2 == 2);
  CHECK(mesh.faces[1].v0 == 0);
  CHECK(mesh.faces[1].v1 == 2);
  CHECK(mesh.faces[1].v2 == 3);
}

/** @id TEST-POTTERY-001-004
 * @verifies REQ-POTTERY-001
 */
TEST_CASE(
    "TEST-POTTERY-001-004: OBJ形式のメッシュは頂点・面・テクスチャ座標・MTL由来の頂点色を保持したまま既定ミリメートルとして読み込まれる") {
  ImportResult result = importFragment("tests/fixtures/textured_triangle.obj");

  REQUIRE(std::holds_alternative<FragmentMesh>(result));
  const FragmentMesh& mesh = std::get<FragmentMesh>(result);

  REQUIRE(mesh.vertices.size() == 3);
  CHECK(mesh.vertices[1].x == doctest::Approx(5.0).epsilon(kEpsilon));
  CHECK(mesh.vertices[2].y == doctest::Approx(5.0).epsilon(kEpsilon));

  REQUIRE(mesh.faces.size() == 1);
  CHECK(mesh.faces[0].v0 == 0);
  CHECK(mesh.faces[0].v1 == 1);
  CHECK(mesh.faces[0].v2 == 2);

  REQUIRE(mesh.texCoords.size() == 3);
  CHECK(mesh.texCoords[1].u == doctest::Approx(1.0).epsilon(kEpsilon));
  CHECK(mesh.texCoords[2].v == doctest::Approx(1.0).epsilon(kEpsilon));

  REQUIRE(mesh.colors.size() == 3);
  CHECK(mesh.colors[0].r == 255);
  CHECK(mesh.colors[0].g == 0);
  CHECK(mesh.colors[0].b == 0);
}

/** @id TEST-POTTERY-001-005
 * @verifies REQ-POTTERY-001
 */
TEST_CASE("TEST-POTTERY-001-005: OBJ形式のメッシュに法線情報(vn)が含まれる場合、頂点法線が保持されたまま読み込まれる") {
  ImportResult result = importFragment("tests/fixtures/obj_with_normals.obj");

  REQUIRE(std::holds_alternative<FragmentMesh>(result));
  const FragmentMesh& mesh = std::get<FragmentMesh>(result);

  REQUIRE(mesh.vertices.size() == 3);
  REQUIRE(mesh.normals.size() == 3);
  CHECK(mesh.normals[0].z == doctest::Approx(1.0).epsilon(kEpsilon));
  CHECK(mesh.normals[1].z == doctest::Approx(1.0).epsilon(kEpsilon));
  CHECK(mesh.normals[2].y == doctest::Approx(1.0).epsilon(kEpsilon));
}

/** @id TEST-POTTERY-002-002
 * @verifies REQ-POTTERY-002
 */
TEST_CASE("TEST-POTTERY-002-002: 破損したSTL/OBJファイルはエラーとして報告され、既存の読込済み破片には影響しない") {
  ImportResult firstOk = importFragment(kFixtureXyzOnly);
  REQUIRE(std::holds_alternative<FragmentMesh>(firstOk));

  ImportResult corruptedStl = importFragment("tests/fixtures/corrupted.stl");
  REQUIRE(std::holds_alternative<ImportError>(corruptedStl));

  ImportResult corruptedObj = importFragment("tests/fixtures/corrupted.obj");
  REQUIRE(std::holds_alternative<ImportError>(corruptedObj));

  ImportResult secondOk = importFragment(kFixtureXyzOnly);
  REQUIRE(std::holds_alternative<FragmentMesh>(secondOk));
  CHECK(std::get<FragmentMesh>(secondOk).vertices.size() ==
        std::get<FragmentMesh>(firstOk).vertices.size());
}
