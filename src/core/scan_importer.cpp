#include "scan_importer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

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

// OBJテキストの1行を空白区切りで分割する。
std::vector<std::string> splitWhitespace(const std::string& line) {
  std::istringstream stream(line);
  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

// "v"/"v/vt"/"v//vn"/"v/vt/vn" 形式の面頂点参照からvn(法線)インデックスのみを取り除く。
std::string stripFaceVertexNormalIndex(const std::string& vertexRef) {
  const auto firstSlash = vertexRef.find('/');
  if (firstSlash == std::string::npos) {
    return vertexRef;  // "v" のみ。
  }
  const auto secondSlash = vertexRef.find('/', firstSlash + 1);
  if (secondSlash == std::string::npos) {
    return vertexRef;  // "v/vt" — vnを含まないためそのまま。
  }
  if (secondSlash == firstSlash + 1) {
    return vertexRef.substr(0, firstSlash);  // "v//vn" — vtが空なので"v"のみ残す。
  }
  return vertexRef.substr(0, secondSlash);  // "v/vt/vn" — "v/vt"のみ残す。
}

// pcl::io::loadOBJFile(path, TextureMesh&) はPCL 1.14において、入力OBJに
// vn(法線)行が含まれる場合にクラッシュする既知の不具合があるため、
// テクスチャ座標・マテリアル色の抽出専用にvn情報を除去した一時コピーを作る。
// mtllib等の相対パス参照を壊さないよう、元ファイルと同じディレクトリに書き出す。
std::string writeObjWithoutNormals(const std::string& filePath) {
  std::ifstream input(filePath);
  if (!input) {
    return "";
  }

  std::ostringstream stripped;
  std::string line;
  while (std::getline(input, line)) {
    const auto tokens = splitWhitespace(line);
    if (!tokens.empty() && tokens.front() == "vn") {
      continue;
    }
    if (!tokens.empty() && tokens.front() == "f") {
      stripped << "f";
      for (std::size_t i = 1; i < tokens.size(); ++i) {
        stripped << ' ' << stripFaceVertexNormalIndex(tokens[i]);
      }
      stripped << '\n';
      continue;
    }
    stripped << line << '\n';
  }

  const std::string tempPath = filePath + ".pfr_no_vn_tmp.obj";
  std::ofstream output(tempPath, std::ios::trunc);
  if (!output) {
    return "";
  }
  output << stripped.str();
  return tempPath;
}

// vn(法線)を含むOBJファイルから、法線情報のみをPolygonMesh経由（クラッシュしない
// 読込経路）で抽出する。頂点順序・数がtextureMeshと一致する場合のみ有効とする。
std::vector<Vec3> tryExtractObjNormals(const std::string& filePath, std::size_t expectedVertexCount) {
  pcl::PolygonMesh polygonMesh;
  if (pcl::io::loadOBJFile(filePath, polygonMesh) < 0) {
    return {};
  }
  const bool hasNormals = pcl::getFieldIndex(polygonMesh.cloud, "normal_x") >= 0 &&
                           pcl::getFieldIndex(polygonMesh.cloud, "normal_y") >= 0 &&
                           pcl::getFieldIndex(polygonMesh.cloud, "normal_z") >= 0;
  if (!hasNormals) {
    return {};
  }

  pcl::PointCloud<pcl::PointNormal> cloud;
  pcl::fromPCLPointCloud2(polygonMesh.cloud, cloud);
  if (cloud.size() != expectedVertexCount) {
    // 法線とテクスチャ座標で頂点展開数が食い違う場合は、破損データを混入させない
    // ため法線抽出を諦める（既知の限界。REQ-POTTERY-001の色/テクスチャ保持を優先）。
    return {};
  }

  std::vector<Vec3> normals;
  normals.reserve(cloud.size());
  for (const auto& point : cloud.points) {
    normals.push_back(Vec3{point.normal_x, point.normal_y, point.normal_z});
  }
  return normals;
}

// ファイル中に "usemtl" 行（マテリアル参照）が存在するかを調べる。
// pcl::io::loadOBJFile(path, TextureMesh&) はPCL 1.14において、マテリアル参照を
// 一切持たないOBJファイルでクラッシュする既知の不具合があるため、その判定に使う。
bool containsMaterialReference(const std::string& filePath) {
  std::ifstream input(filePath);
  std::string line;
  while (std::getline(input, line)) {
    const auto tokens = splitWhitespace(line);
    if (!tokens.empty() && tokens.front() == "usemtl") {
      return true;
    }
  }
  return false;
}

// 頂点・面情報のみをPolygonMesh経由で読み込む（マテリアル/テクスチャ座標を持たない
// OBJ向け。TextureMesh読込経路のPCL既知不具合を回避するため）。
ImportResult importObjMeshWithoutMaterial(const std::string& filePath, double unitToMillimeterFactor) {
  pcl::PolygonMesh polygonMesh;
  if (pcl::io::loadOBJFile(filePath, polygonMesh) < 0) {
    return ImportError{filePath, "OBJファイルの読み込みに失敗しました（破損または非対応形式）。"};
  }
  if (polygonMesh.polygons.empty()) {
    return ImportError{filePath, "必須のジオメトリ情報（面情報）が欠如しています。"};
  }

  const bool hasNormals = pcl::getFieldIndex(polygonMesh.cloud, "normal_x") >= 0 &&
                           pcl::getFieldIndex(polygonMesh.cloud, "normal_y") >= 0 &&
                           pcl::getFieldIndex(polygonMesh.cloud, "normal_z") >= 0;

  pcl::PointCloud<pcl::PointNormal> cloud;
  pcl::fromPCLPointCloud2(polygonMesh.cloud, cloud);

  FragmentMesh mesh;
  mesh.sourceFilePath = filePath;
  mesh.vertices.reserve(cloud.size());
  if (hasNormals) {
    mesh.normals.reserve(cloud.size());
  }
  for (const auto& point : cloud.points) {
    mesh.vertices.push_back(Vec3{point.x * unitToMillimeterFactor,
                                  point.y * unitToMillimeterFactor,
                                  point.z * unitToMillimeterFactor});
    if (hasNormals) {
      mesh.normals.push_back(Vec3{point.normal_x, point.normal_y, point.normal_z});
    }
  }

  mesh.faces.reserve(polygonMesh.polygons.size());
  for (const auto& polygon : polygonMesh.polygons) {
    if (polygon.vertices.size() != 3) {
      return ImportError{filePath, "三角形以外の面を含むOBJファイルには対応していません。"};
    }
    mesh.faces.push_back(
        Triangle{polygon.vertices[0], polygon.vertices[1], polygon.vertices[2]});
  }

  return mesh;
}

// OBJファイル（三角メッシュ、テクスチャ座標、関連MTLの拡散色を頂点色として）を読み込む。
ImportResult importObjMesh(const std::string& filePath, double unitToMillimeterFactor) {
  // マテリアル参照(usemtl)を一切持たないOBJは、TextureMesh読込経路のPCL既知不具合を
  // 回避するため、マテリアル/テクスチャ座標を持たないPolygonMesh経路で読み込む。
  // この経路であれば法線(vn)もそのまま保持できる。
  if (!containsMaterialReference(filePath)) {
    return importObjMeshWithoutMaterial(filePath, unitToMillimeterFactor);
  }

  const bool hasVnLines = [&filePath]() {
    std::ifstream input(filePath);
    std::string line;
    while (std::getline(input, line)) {
      const auto tokens = splitWhitespace(line);
      if (!tokens.empty() && tokens.front() == "vn") {
        return true;
      }
    }
    return false;
  }();

  // vn行を含むファイルはTextureMesh読込経路のPCL既知不具合を避けるため、
  // vn除去済みの一時コピーを色/テクスチャ座標抽出に用いる。
  std::string textureLoadPath = filePath;
  std::string tempPathToCleanup;
  if (hasVnLines) {
    tempPathToCleanup = writeObjWithoutNormals(filePath);
    if (tempPathToCleanup.empty()) {
      return ImportError{filePath, "OBJファイルの読み込みに失敗しました（破損または非対応形式）。"};
    }
    textureLoadPath = tempPathToCleanup;
  }
  struct TempFileGuard {
    const std::string& path;
    ~TempFileGuard() {
      if (!path.empty()) {
        std::remove(path.c_str());
      }
    }
  } tempFileGuard{tempPathToCleanup};

  pcl::TextureMesh textureMesh;
  if (pcl::io::loadOBJFile(textureLoadPath, textureMesh) < 0) {
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

  if (hasVnLines) {
    std::vector<Vec3> normals = tryExtractObjNormals(filePath, cloud.size());
    if (!normals.empty()) {
      mesh.normals = std::move(normals);
    }
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
