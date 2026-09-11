#include "scan_importer.hpp"

#include <algorithm>
#include <cctype>

#include <pcl/PCLPointCloud2.h>
#include <pcl/common/io.h>
#include <pcl/conversions.h>
#include <pcl/io/pcd_io.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

namespace kintsugi::core {

namespace {

std::string toLowerExtension(const std::string& filePath) {
  const auto dotPos = filePath.find_last_of('.');
  if (dotPos == std::string::npos) {
    return "";
  }
  std::string ext = filePath.substr(dotPos + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

// 点群(PCD)ファイルを読み込み、単位換算済みのFragmentMeshを構築する。
// 法線・色フィールドが元ファイルに存在しない場合、対応するベクトルは空のままとする。
ImportResult importPointCloud(const std::string& filePath, double unitToMillimeterFactor) {
  pcl::PCLPointCloud2 blob;
  if (pcl::io::loadPCDFile(filePath, blob) < 0) {
    return ImportError{filePath, "点群ファイルの読み込みに失敗しました（破損または非対応形式）。"};
  }
  if (pcl::getFieldIndex(blob, "x") < 0 || pcl::getFieldIndex(blob, "y") < 0 ||
      pcl::getFieldIndex(blob, "z") < 0) {
    return ImportError{filePath, "必須のジオメトリ情報（x/y/z座標）が欠如しています。"};
  }

  const bool hasNormals = pcl::getFieldIndex(blob, "normal_x") >= 0 &&
                           pcl::getFieldIndex(blob, "normal_y") >= 0 &&
                           pcl::getFieldIndex(blob, "normal_z") >= 0;
  const bool hasColor =
      pcl::getFieldIndex(blob, "rgb") >= 0 || pcl::getFieldIndex(blob, "rgba") >= 0;

  pcl::PointCloud<pcl::PointXYZRGBNormal> cloud;
  pcl::fromPCLPointCloud2(blob, cloud);

  FragmentMesh mesh;
  mesh.sourceFilePath = filePath;
  mesh.vertices.reserve(cloud.size());
  if (hasNormals) {
    mesh.normals.reserve(cloud.size());
  }
  if (hasColor) {
    mesh.colors.reserve(cloud.size());
  }

  for (const auto& point : cloud.points) {
    mesh.vertices.push_back(Vec3{point.x * unitToMillimeterFactor,
                                  point.y * unitToMillimeterFactor,
                                  point.z * unitToMillimeterFactor});
    if (hasNormals) {
      mesh.normals.push_back(Vec3{point.normal_x, point.normal_y, point.normal_z});
    }
    if (hasColor) {
      mesh.colors.push_back(ColorRGB{point.r, point.g, point.b});
    }
  }

  return mesh;
}

}  // namespace

double lengthUnitToMillimeterFactor(LengthUnit unit) {
  switch (unit) {
    case LengthUnit::Millimeter:
      return 1.0;
    case LengthUnit::Centimeter:
      return 10.0;
    case LengthUnit::Meter:
      return 1000.0;
    case LengthUnit::Inch:
      return 25.4;
  }
  return 1.0;
}

ImportResult importFragment(const std::string& filePath, std::optional<LengthUnit> explicitUnit) {
  const double factor =
      explicitUnit.has_value() ? lengthUnitToMillimeterFactor(*explicitUnit) : 1.0;
  const std::string ext = toLowerExtension(filePath);

  if (ext == "pcd") {
    return importPointCloud(filePath, factor);
  }

  return ImportError{filePath, "非対応のファイル形式です: ." + ext};
}

std::vector<FormatDescriptor> listSupportedFormats() {
  return {
      FormatDescriptor{"Point Cloud Data (PCD)", {"pcd"}},
  };
}

}  // namespace kintsugi::core
