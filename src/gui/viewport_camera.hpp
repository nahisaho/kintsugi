#pragma once

namespace kintsugi::gui {

/** @id CODE-POTTERY-009
 * @implements REQ-POTTERY-014
 * @design DES-POTTERY-009
 */

// 3次元空間中の1点。core::Vec3と同一構造だが、GUI層をcoreから独立させる
// （ADR-0002：UI変更がコア計算層の性能特性に影響しないための境界分離）ため、
// 意図的にGUI層固有の型として定義する。
struct Point3D {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

// 3Dビューア（VTKベース、ADR-0002）のカメラ状態を、焦点(focalPoint)を中心とする
// 球面座標（方位角azimuth・仰角elevation・距離distance）とパンオフセットで表す
// オービットカメラモデル。実際のVTK描画パイプラインへの反映はDES-POTTERY-009の
// 薄いUIシェル（本コンポーネントの外側）が担い、本クラスは視点操作
// （回転・拡大縮小・移動）がカメラパラメータへ正しく反映されることのみを
// 扱う（REQ-POTTERY-014：ヘッドレス環境でも検証可能な形での実装）。
class ViewportCamera {
 public:
  explicit ViewportCamera(Point3D focalPoint = Point3D{0.0, 0.0, 0.0}, double initialDistanceMm = 500.0);

  // 現在のカメラ状態から算出される視点（eye）位置。
  Point3D eyePosition() const;

  double azimuthDeg() const { return azimuthDeg_; }
  double elevationDeg() const { return elevationDeg_; }
  double distanceMm() const { return distanceMm_; }
  Point3D focalPoint() const { return focalPoint_; }

  // 回転操作：方位角・仰角を相対的に変更する（REQ-POTTERY-014の「回転」）。
  // 仰角はジンバルロック回避のため[-89, 89]度にクランプする。
  void rotate(double deltaAzimuthDeg, double deltaElevationDeg);

  // 拡大縮小操作：焦点からの距離を相対的に変更する（REQ-POTTERY-014の
  // 「拡大縮小」）。factorが1より大きいほど拡大（距離が縮む）、
  // 最小距離未満には縮まない。
  void zoom(double factor);

  // 移動操作：焦点をビュー平面内で平行移動する（REQ-POTTERY-014の「移動」）。
  void pan(double dxMm, double dyMm);

 private:
  Point3D focalPoint_;
  double azimuthDeg_ = 0.0;
  double elevationDeg_ = 0.0;
  double distanceMm_ = 500.0;
};

}  // namespace kintsugi::gui
