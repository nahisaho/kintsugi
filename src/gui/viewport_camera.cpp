#include "viewport_camera.hpp"

#include <algorithm>
#include <cmath>

namespace kintsugi::gui {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinDistanceMm = 1.0;
constexpr double kMaxElevationDeg = 89.0;

double toRadians(double degrees) { return degrees * kPi / 180.0; }

Point3D add(const Point3D& a, const Point3D& b) { return Point3D{a.x + b.x, a.y + b.y, a.z + b.z}; }

Point3D scale(const Point3D& v, double s) { return Point3D{v.x * s, v.y * s, v.z * s}; }

Point3D cross(const Point3D& a, const Point3D& b) {
  return Point3D{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double length(const Point3D& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Point3D normalize(const Point3D& v) {
  const double len = length(v);
  if (len < 1e-9) return Point3D{0.0, 0.0, 0.0};
  return scale(v, 1.0 / len);
}

// 方位角azimuth・仰角elevationから、焦点を基準とした視点方向の単位ベクトル
// （焦点→視点方向）を求める。Z軸を鉛直上向き（DES-POTTERY-009のVTKビュー
// アップ設定および壺形状データのZ=高さ軸と一致させる）として扱うため、
// elevationはZ成分を、azimuthはXY平面内の回転を表す。
Point3D directionFromAngles(double azimuthDeg, double elevationDeg) {
  const double azimuthRad = toRadians(azimuthDeg);
  const double elevationRad = toRadians(elevationDeg);
  return Point3D{
      std::cos(elevationRad) * std::sin(azimuthRad),
      std::cos(elevationRad) * std::cos(azimuthRad),
      std::sin(elevationRad),
  };
}

}  // namespace

ViewportCamera::ViewportCamera(Point3D focalPoint, double initialDistanceMm)
    : focalPoint_(focalPoint), distanceMm_(std::max(kMinDistanceMm, initialDistanceMm)) {}

Point3D ViewportCamera::eyePosition() const {
  const Point3D direction = directionFromAngles(azimuthDeg_, elevationDeg_);
  return add(focalPoint_, scale(direction, distanceMm_));
}

void ViewportCamera::rotate(double deltaAzimuthDeg, double deltaElevationDeg) {
  azimuthDeg_ += deltaAzimuthDeg;
  elevationDeg_ = std::clamp(elevationDeg_ + deltaElevationDeg, -kMaxElevationDeg, kMaxElevationDeg);
}

void ViewportCamera::zoom(double factor) {
  if (factor <= 0.0) {
    return;  // 不正な倍率は無視し、現在の距離を維持する。
  }
  distanceMm_ = std::max(kMinDistanceMm, distanceMm_ / factor);
}

void ViewportCamera::pan(double dxMm, double dyMm) {
  // 現在の視線方向(焦点→視点)から、ビュー平面内の右方向・上方向ベクトルを
  // 求め、その平面内で焦点を平行移動する。視点(eye)は焦点+球面座標オフセット
  // で導出されるため、焦点の移動により視点も追随して移動する。
  static const Point3D kWorldUp{0.0, 0.0, 1.0};
  const Point3D viewDirection = normalize(directionFromAngles(azimuthDeg_, elevationDeg_));
  Point3D right = cross(viewDirection, kWorldUp);
  if (length(right) < 1e-6) {
    // 視線が真上/真下を向いておりworldUpと平行な場合は代替の基準軸を用いる。
    right = Point3D{1.0, 0.0, 0.0};
  } else {
    right = normalize(right);
  }
  const Point3D up = normalize(cross(right, viewDirection));

  focalPoint_ = add(focalPoint_, add(scale(right, dxMm), scale(up, dyMm)));
}

}  // namespace kintsugi::gui
