#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "pose_conflict.hpp"

namespace kintsugi::core {

// 頂点色（0-255）。テクスチャ/頂点色を持たない入力では colors は空のままとなる。
struct ColorRGB {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

// テクスチャUV座標。テクスチャ座標を持たない入力では texCoords は空のままとなる。
struct Vec2 {
  double u = 0.0;
  double v = 0.0;
};

// 三角形面を構成する頂点インデックス（vertices への添字）。点群には面情報がないため空。
struct Triangle {
  std::size_t v0 = 0;
  std::size_t v1 = 0;
  std::size_t v2 = 0;
};

// インポート後の破片形状データ（REQ-POTTERY-001）。
// 座標は常にミリメートル単位に正規化される。法線・色・テクスチャ座標・面情報は
// 入力に含まれない場合は空のままとなる。
struct FragmentMesh {
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<ColorRGB> colors;
  std::vector<Vec2> texCoords;
  std::vector<Triangle> faces;
  std::string sourceFilePath;
};

// 利用者が明示的に指定できる長さ単位。未指定時は既定でミリメートルとみなす。
enum class LengthUnit {
  Millimeter,
  Centimeter,
  Meter,
  Inch,
};

// LengthUnit の値をミリメートルへ換算する係数を返す。
double lengthUnitToMillimeterFactor(LengthUnit unit);

// 読込失敗（破損・非対応形式・必須ジオメトリ欠如）を表す（REQ-POTTERY-002）。
struct ImportError {
  std::string filePath;
  std::string message;
};

// importFragment の戻り値。成功時は FragmentMesh、失敗時は ImportError。
using ImportResult = std::variant<FragmentMesh, ImportError>;

// listSupportedFormats が返す、対応フォーマットの説明。
struct FormatDescriptor {
  std::string formatName;
  std::vector<std::string> extensions;
};

}  // namespace kintsugi::core
