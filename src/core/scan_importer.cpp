#include "scan_importer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include <pcl/PCLPointCloud2.h>
#include <pcl/PolygonMesh.h>
#include <pcl/TextureMesh.h>
#include <pcl/common/io.h>
#include <pcl/conversions.h>
#include <pcl/io/obj_io.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/vtk_lib_io.h>
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

// STLファイル（三角メッシュ、頂点色・法線・テクスチャ座標は持たない）を読み込む。
ImportResult importStlMesh(const std::string& filePath, double unitToMillimeterFactor) {
  pcl::PolygonMesh polygonMesh;
  if (pcl::io::loadPolygonFileSTL(filePath, polygonMesh) <= 0) {
    return ImportError{filePath, "STLファイルの読み込みに失敗しました（破損または非対応形式）。"};
  }
  if (polygonMesh.polygons.empty()) {
    return ImportError{filePath, "必須のジオメトリ情報（面情報）が欠如しています。"};
  }

  pcl::PointCloud<pcl::PointXYZ> cloud;
  pcl::fromPCLPointCloud2(polygonMesh.cloud, cloud);

  FragmentMesh mesh;
  mesh.sourceFilePath = filePath;
  mesh.vertices.reserve(cloud.size());
  for (const auto& point : cloud.points) {
    mesh.vertices.push_back(Vec3{point.x * unitToMillimeterFactor,
                                  point.y * unitToMillimeterFactor,
                                  point.z * unitToMillimeterFactor});
  }

  mesh.faces.reserve(polygonMesh.polygons.size());
  for (const auto& polygon : polygonMesh.polygons) {
    if (polygon.vertices.size() != 3) {
      return ImportError{filePath, "三角形以外の面を含むSTLファイルには対応していません。"};
    }
    mesh.faces.push_back(
        Triangle{polygon.vertices[0], polygon.vertices[1], polygon.vertices[2]});
  }

  return mesh;
}

// OBJファイル（三角メッシュ、テクスチャ座標、関連MTLの拡散色を頂点色として）を読み込む。
ImportResult importObjMesh(const std::string& filePath, double unitToMillimeterFactor) {
  pcl::TextureMesh textureMesh;
  if (pcl::io::loadOBJFile(filePath, textureMesh) < 0) {
    return ImportError{filePath, "OBJファイルの読み込みに失敗しました（破損または非対応形式）。"};
  }
  if (textureMesh.tex_polygons.empty() ||
      std::all_of(textureMesh.tex_polygons.begin(), textureMesh.tex_polygons.end(),
                  [](const auto& group) { return group.empty(); })) {
    return ImportError{filePath, "必須のジオメトリ情報（面情報）が欠如しています。"};
  }

  pcl::PointCloud<pcl::PointXYZ> cloud;
  pcl::fromPCLPointCloud2(textureMesh.cloud, cloud);

  FragmentMesh mesh;
  mesh.sourceFilePath = filePath;
  mesh.vertices.reserve(cloud.size());
  for (const auto& point : cloud.points) {
    mesh.vertices.push_back(Vec3{point.x * unitToMillimeterFactor,
                                  point.y * unitToMillimeterFactor,
                                  point.z * unitToMillimeterFactor});
  }

  // 頂点色は、その頂点を参照する最初の面が属するマテリアルのKd(拡散色)を採用する。
  mesh.colors.assign(cloud.size(), ColorRGB{});
  std::vector<bool> colorAssigned(cloud.size(), false);
  bool anyMaterialColor = false;

  for (std::size_t groupIndex = 0; groupIndex < textureMesh.tex_polygons.size(); ++groupIndex) {
    ColorRGB groupColor;
    bool hasGroupColor = false;
    if (groupIndex < textureMesh.tex_materials.size()) {
      const auto& material = textureMesh.tex_materials[groupIndex];
      groupColor = ColorRGB{
          static_cast<std::uint8_t>(std::round(std::clamp(material.tex_Kd.r, 0.0f, 1.0f) * 255.0f)),
          static_cast<std::uint8_t>(std::round(std::clamp(material.tex_Kd.g, 0.0f, 1.0f) * 255.0f)),
          static_cast<std::uint8_t>(std::round(std::clamp(material.tex_Kd.b, 0.0f, 1.0f) * 255.0f))};
      hasGroupColor = true;
      anyMaterialColor = true;
    }

    for (const auto& polygon : textureMesh.tex_polygons[groupIndex]) {
      if (polygon.vertices.size() != 3) {
        return ImportError{filePath, "三角形以外の面を含むOBJファイルには対応していません。"};
      }
      mesh.faces.push_back(
          Triangle{polygon.vertices[0], polygon.vertices[1], polygon.vertices[2]});
      if (hasGroupColor) {
        for (const auto vertexIndex : polygon.vertices) {
          if (!colorAssigned[vertexIndex]) {
            mesh.colors[vertexIndex] = groupColor;
            colorAssigned[vertexIndex] = true;
          }
        }
      }
    }
  }
  if (!anyMaterialColor) {
    mesh.colors.clear();
  }

  if (!textureMesh.tex_coordinates.empty()) {
    mesh.texCoords.reserve(cloud.size());
    for (const auto& uv : textureMesh.tex_coordinates.front()) {
      mesh.texCoords.push_back(Vec2{uv[0], uv[1]});
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
  if (ext == "stl") {
    return importStlMesh(filePath, factor);
  }
  if (ext == "obj") {
    return importObjMesh(filePath, factor);
  }

  return ImportError{filePath, "非対応のファイル形式です: ." + ext};
}

std::vector<FormatDescriptor> listSupportedFormats() {
  return {
      FormatDescriptor{"Point Cloud Data (PCD)", {"pcd"}},
      FormatDescriptor{"Stereolithography (STL)", {"stl"}},
      FormatDescriptor{"Wavefront OBJ (+MTL)", {"obj"}},
  };
}

}  // namespace kintsugi::core
