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
