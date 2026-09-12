#include "mesh_export.hpp"

#include <sstream>

namespace kintsugi::core {

namespace {

// PropagatedPoseの回転（単位四元数）でベクトルvを回転する。missing_part_estimation.cpp
// と概念的に同じ処理だが、pose_conflict.hppの既存Green済みTDD証跡を変更するリスクを
// 避けるため、当該ファイルと同様にローカルへ再実装する（既存の適応方針を踏襲）。
Vec3 rotateVector(const Quaternion& q, const Vec3& v) {
  const double qx = q.x, qy = q.y, qz = q.z, qw = q.w;
  const double tx = 2.0 * (qy * v.z - qz * v.y);
  const double ty = 2.0 * (qz * v.x - qx * v.z);
  const double tz = 2.0 * (qx * v.y - qy * v.x);
  return Vec3{
      v.x + qw * tx + (qy * tz - qz * ty),
      v.y + qw * ty + (qz * tx - qx * tz),
      v.z + qw * tz + (qx * ty - qy * tx),
  };
}

Vec3 transformVertex(const PropagatedPose& pose, const Vec3& vertex) {
  const Vec3 rotated = rotateVector(pose.rotation, vertex);
  return Vec3{rotated.x + pose.translationMm.x, rotated.y + pose.translationMm.y,
              rotated.z + pose.translationMm.z};
}

std::string writeStlAscii(const std::vector<Vec3>& vertices, const std::vector<Triangle>& faces) {
  std::ostringstream out;
  out << "solid kintsugi_integrated_mesh\n";
  for (const Triangle& tri : faces) {
    const Vec3& a = vertices[tri.v0];
    const Vec3& b = vertices[tri.v1];
    const Vec3& c = vertices[tri.v2];
    out << "facet normal 0 0 0\n";
    out << "outer loop\n";
    out << "vertex " << a.x << " " << a.y << " " << a.z << "\n";
    out << "vertex " << b.x << " " << b.y << " " << b.z << "\n";
    out << "vertex " << c.x << " " << c.y << " " << c.z << "\n";
    out << "endloop\n";
    out << "endfacet\n";
  }
  out << "endsolid kintsugi_integrated_mesh\n";
  return out.str();
}

std::string writeObj(const std::vector<Vec3>& vertices, const std::vector<Triangle>& faces) {
  std::ostringstream out;
  for (const Vec3& v : vertices) {
    out << "v " << v.x << " " << v.y << " " << v.z << "\n";
  }
  // usemtlディレクティブを付与する。マテリアル定義(mtllib)自体は必須ではないが、
  // PCLのOBJリーダーはusemtl不在のポリゴングループでクラッシュする既知の問題が
  // あるため、既定マテリアル名を明示して安全に読み込めるようにする。
  out << "usemtl kintsugi_default\n";
  for (const Triangle& tri : faces) {
    // OBJの頂点インデックスは1始まり。
    out << "f " << (tri.v0 + 1) << " " << (tri.v1 + 1) << " " << (tri.v2 + 1) << "\n";
  }
  return out.str();
}

}  // namespace

ExportedFile exportIntegratedMesh(const AssemblyState& assemblyState, const std::vector<FragmentMesh>& fragments,
                                   MeshExportFormat format) {
  std::vector<Vec3> mergedVertices;
  std::vector<Triangle> mergedFaces;

  for (const std::size_t fragmentId : assemblyState.fragmentIds) {
    // DES-POTTERY-007のbuildUnmatchedFragmentReportと同一の基準
    // （hasAcceptedJoin）で未接合破片を除外する。
    if (!assemblyState.hasAcceptedJoin(fragmentId)) {
      continue;
    }
    if (fragmentId >= fragments.size()) {
      continue;
    }
    const auto pose = assemblyState.resolvedPose(fragmentId);
    if (!pose.has_value()) {
      continue;  // hasAcceptedJointがtrueであれば通常発生しないが、念のため防御する。
    }

    const std::size_t vertexOffset = mergedVertices.size();
    for (const Vec3& vertex : fragments[fragmentId].vertices) {
      mergedVertices.push_back(transformVertex(*pose, vertex));
    }
    for (const Triangle& tri : fragments[fragmentId].faces) {
      mergedFaces.push_back(Triangle{tri.v0 + vertexOffset, tri.v1 + vertexOffset, tri.v2 + vertexOffset});
    }
  }

  ExportedFile result;
  result.format = format;
  result.content = (format == MeshExportFormat::Stl) ? writeStlAscii(mergedVertices, mergedFaces)
                                                      : writeObj(mergedVertices, mergedFaces);
  return result;
}

}  // namespace kintsugi::core
