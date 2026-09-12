#include <cmath>

#include "../third_party/doctest/doctest.h"
#include "../src/gui/viewport_camera.hpp"

using kintsugi::gui::Point3D;
using kintsugi::gui::ViewportCamera;

namespace {

double distanceBetween(const Point3D& a, const Point3D& b) {
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  const double dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

/** @id TEST-POTTERY-014-001
 * @verifies REQ-POTTERY-014
 */
// 回転・拡大縮小・移動の各視点操作が、3Dビューアのカメラ状態
// （視点位置・焦点からの距離・焦点位置）へ正しく反映されること。
TEST_CASE("TEST-POTTERY-014-001 ViewportCamera reflects rotate/zoom/pan viewpoint operations") {
  ViewportCamera camera(Point3D{0.0, 0.0, 0.0}, 500.0);
  const Point3D initialEye = camera.eyePosition();
  const double initialDistance = camera.distanceMm();

  SUBCASE("rotate changes azimuth/elevation and therefore the eye position") {
    camera.rotate(45.0, 10.0);
    CHECK(camera.azimuthDeg() == doctest::Approx(45.0));
    CHECK(camera.elevationDeg() == doctest::Approx(10.0));
    // 焦点からの距離は回転操作では変化しないこと。
    CHECK(camera.distanceMm() == doctest::Approx(initialDistance));
    // 視点位置自体は回転により変化すること。
    CHECK(distanceBetween(camera.eyePosition(), initialEye) > 1.0);
  }

  SUBCASE("rotate clamps elevation to avoid gimbal lock") {
    camera.rotate(0.0, 500.0);
    CHECK(camera.elevationDeg() <= 89.0);
    camera.rotate(0.0, -1000.0);
    CHECK(camera.elevationDeg() >= -89.0);
  }

  SUBCASE("zoom in reduces distance from the focal point, zoom out increases it") {
    camera.zoom(2.0);
    CHECK(camera.distanceMm() == doctest::Approx(initialDistance / 2.0));
    camera.zoom(0.5);
    CHECK(camera.distanceMm() == doctest::Approx(initialDistance));
  }

  SUBCASE("zoom never reduces distance below the minimum threshold") {
    for (int i = 0; i < 100; ++i) {
      camera.zoom(10.0);
    }
    CHECK(camera.distanceMm() >= 1.0);
  }

  SUBCASE("pan moves the focal point (and therefore the eye position) in the view plane") {
    const Point3D initialFocal = camera.focalPoint();
    camera.pan(50.0, 20.0);
    CHECK(distanceBetween(camera.focalPoint(), initialFocal) > 1.0);
    // 焦点からの距離自体はパン操作では変化しないこと。
    CHECK(camera.distanceMm() == doctest::Approx(initialDistance));
  }
}

/** @id TEST-POTTERY-014-002
 * @verifies REQ-POTTERY-014
 */
// 3DビューアはZ軸を鉛直上向き(ViewUp)として描画する(DES-POTTERY-009)。
// 初期状態（azimuth=0, elevation=0）の視点方向(焦点→視点)がZ軸と平行だと、
// VTKのカメラ変換が特異になり画面に何も描画されなくなる不具合があった
// （視点方向とビューアップベクトルが同一直線上になるため）。本テストは、
// 初期状態を含むあらゆる回転状態で視点方向がZ軸と平行にならないことを
// 保証する回帰テスト。
TEST_CASE("TEST-POTTERY-014-002 ViewportCamera view direction stays non-degenerate against the Z-up vector") {
  ViewportCamera camera(Point3D{0.0, 0.0, 0.0}, 500.0);

  auto isParallelToZAxis = [](const Point3D& eye, const Point3D& focal) {
    const double dx = eye.x - focal.x;
    const double dy = eye.y - focal.y;
    const double dz = eye.z - focal.z;
    const double horizontalLen = std::sqrt(dx * dx + dy * dy);
    // 水平成分がほぼゼロ、すなわち視点方向がZ軸とほぼ平行な場合に真。
    return horizontalLen < 1e-6 && std::fabs(dz) > 1e-6;
  };

  // 初期状態（画面に何も表示されなかった不具合の再現条件）。
  CHECK_FALSE(isParallelToZAxis(camera.eyePosition(), camera.focalPoint()));

  // 回転操作後も同様に非退化であること。
  camera.rotate(90.0, 0.0);
  CHECK_FALSE(isParallelToZAxis(camera.eyePosition(), camera.focalPoint()));
}

