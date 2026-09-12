#include "project_persistence.hpp"

#include <openssl/evp.h>
#include <zip.h>
#include <unzip.h>

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace kintsugi::core {

namespace {

using nlohmann::json;

// --- SHA-256（ファイル参照ハッシュ、REQ-POTTERY-015/024） ---------------

std::string sha256HexOfFile(const std::string& filePath) {
  std::ifstream in(filePath, std::ios::binary);
  if (!in) {
    // 呼び出し元（computeFragmentSourceRef）が存在確認済みの前提だが、
    // 念のため読込不能時は空文字列を返す（ハッシュ不一致として扱われる）。
    return {};
  }
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  char buffer[8192];
  while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
    EVP_DigestUpdate(ctx, buffer, static_cast<std::size_t>(in.gcount()));
  }
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digestLen = 0;
  EVP_DigestFinal_ex(ctx, digest, &digestLen);
  EVP_MD_CTX_free(ctx);

  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < digestLen; ++i) {
    oss << std::setw(2) << static_cast<int>(digest[i]);
  }
  return oss.str();
}

bool fileExists(const std::string& filePath) {
  std::ifstream in(filePath, std::ios::binary);
  return static_cast<bool>(in);
}

// --- JSONシリアライズ（AssemblyState / EstimatedShapeRecord等） ---------

json toJson(const Vec3& v) { return json{{"x", v.x}, {"y", v.y}, {"z", v.z}}; }
Vec3 vec3FromJson(const json& j) { return Vec3{j.at("x").get<double>(), j.at("y").get<double>(), j.at("z").get<double>()}; }

json toJson(const Quaternion& q) { return json{{"w", q.w}, {"x", q.x}, {"y", q.y}, {"z", q.z}}; }
Quaternion quaternionFromJson(const json& j) {
  return Quaternion{j.at("w").get<double>(), j.at("x").get<double>(), j.at("y").get<double>(), j.at("z").get<double>()};
}

json toJson(const PropagatedPose& pose) {
  return json{{"translationMm", toJson(pose.translationMm)}, {"rotation", toJson(pose.rotation)}};
}
PropagatedPose propagatedPoseFromJson(const json& j) {
  return PropagatedPose{vec3FromJson(j.at("translationMm")), quaternionFromJson(j.at("rotation"))};
}

json toJson(const AcceptedJoin& join) {
  return json{{"fragmentIdA", join.fragmentIdA},
              {"fragmentIdB", join.fragmentIdB},
              {"pose", toJson(join.pose)},
              {"confidenceScore", join.confidenceScore}};
}
AcceptedJoin acceptedJoinFromJson(const json& j) {
  AcceptedJoin join;
  join.fragmentIdA = j.at("fragmentIdA").get<std::size_t>();
  join.fragmentIdB = j.at("fragmentIdB").get<std::size_t>();
  join.pose = propagatedPoseFromJson(j.at("pose"));
  join.confidenceScore = j.at("confidenceScore").get<double>();
  return join;
}

const char* rejectionReasonToString(RejectionReason reason) {
  return reason == RejectionReason::Conflict ? "Conflict" : "UserRejected";
}
RejectionReason rejectionReasonFromString(const std::string& s) {
  return s == "Conflict" ? RejectionReason::Conflict : RejectionReason::UserRejected;
}

json toJson(const RejectedCandidate& candidate) {
  return json{{"fragmentIdA", candidate.fragmentIdA},
              {"fragmentIdB", candidate.fragmentIdB},
              {"confidenceScore", candidate.confidenceScore},
              {"reason", rejectionReasonToString(candidate.reason)}};
}
RejectedCandidate rejectedCandidateFromJson(const json& j) {
  RejectedCandidate candidate;
  candidate.fragmentIdA = j.at("fragmentIdA").get<std::size_t>();
  candidate.fragmentIdB = j.at("fragmentIdB").get<std::size_t>();
  candidate.confidenceScore = j.at("confidenceScore").get<double>();
  candidate.reason = rejectionReasonFromString(j.at("reason").get<std::string>());
  return candidate;
}

json toJson(const AssemblyState& state) {
  json manualOverrides = json::array();
  for (const auto& [fragmentId, pose] : state.manualPoseOverrides) {
    manualOverrides.push_back(json{{"fragmentId", fragmentId}, {"pose", toJson(pose)}});
  }
  json acceptedJoins = json::array();
  for (const auto& join : state.acceptedJoins) acceptedJoins.push_back(toJson(join));
  json rejectedCandidates = json::array();
  for (const auto& candidate : state.rejectedCandidates) rejectedCandidates.push_back(toJson(candidate));

  return json{{"fragmentIds", state.fragmentIds},
              {"acceptedJoins", acceptedJoins},
              {"rejectedCandidates", rejectedCandidates},
              {"manualPoseOverrides", manualOverrides}};
}

// AssemblyStateはacceptedJoins/rejectedCandidates/manualPoseOverridesを
// private書き込み経路（AssemblyOrchestrator）を介さず直接復元する必要が
// あるため、フィールドを直接構築する（AssemblyStateは全メンバpublicの
// 単純な値オブジェクトである）。
AssemblyState assemblyStateFromJson(const json& j) {
  AssemblyState state;
  state.fragmentIds = j.at("fragmentIds").get<std::vector<std::size_t>>();
  for (const auto& jj : j.at("acceptedJoins")) state.acceptedJoins.push_back(acceptedJoinFromJson(jj));
  for (const auto& jj : j.at("rejectedCandidates")) state.rejectedCandidates.push_back(rejectedCandidateFromJson(jj));
  for (const auto& jj : j.at("manualPoseOverrides")) {
    state.manualPoseOverrides[jj.at("fragmentId").get<std::size_t>()] = propagatedPoseFromJson(jj.at("pose"));
  }
  return state;
}

json toJson(const Triangle& t) { return json{{"v0", t.v0}, {"v1", t.v1}, {"v2", t.v2}}; }
Triangle triangleFromJson(const json& j) {
  return Triangle{j.at("v0").get<std::size_t>(), j.at("v1").get<std::size_t>(), j.at("v2").get<std::size_t>()};
}

json toJson(const EstimatedShapeRecord& record) {
  json vertices = json::array();
  for (const auto& v : record.vertices) vertices.push_back(toJson(v));
  json faces = json::array();
  for (const auto& f : record.faces) faces.push_back(toJson(f));
  return json{{"vertices", vertices}, {"faces", faces}};
}
EstimatedShapeRecord estimatedShapeRecordFromJson(const json& j) {
  EstimatedShapeRecord record;
  for (const auto& jj : j.at("vertices")) record.vertices.push_back(vec3FromJson(jj));
  for (const auto& jj : j.at("faces")) record.faces.push_back(triangleFromJson(jj));
  return record;
}

json toJson(const FragmentSourceRef& ref) {
  return json{{"fragmentId", ref.fragmentId}, {"filePath", ref.filePath}, {"sha256Hex", ref.sha256Hex}};
}
FragmentSourceRef fragmentSourceRefFromJson(const json& j) {
  return FragmentSourceRef{j.at("fragmentId").get<std::size_t>(), j.at("filePath").get<std::string>(),
                            j.at("sha256Hex").get<std::string>()};
}

constexpr int kManifestSchemaVersion = 1;

// ProjectState::integrityWarningsは読込結果のみを表すため、保存対象の
// マニフェストには含めない。
std::string serializeManifest(const ProjectState& projectState) {
  json fragmentSources = json::array();
  for (const auto& ref : projectState.fragmentSources) fragmentSources.push_back(toJson(ref));

  json assemblyStates = json::array();
  for (const auto& state : projectState.assemblyStates) assemblyStates.push_back(toJson(state));

  json estimatedShapesByVessel = json::array();
  for (const auto& shapes : projectState.estimatedShapesByVessel) {
    json shapesJson = json::array();
    for (const auto& shape : shapes) shapesJson.push_back(toJson(shape));
    estimatedShapesByVessel.push_back(shapesJson);
  }

  json manifest = {{"schemaVersion", kManifestSchemaVersion},
                    {"fragmentSources", fragmentSources},
                    {"unclassifiedFragmentIds", projectState.unclassifiedFragmentIds},
                    {"assemblyStates", assemblyStates},
                    {"estimatedShapesByVessel", estimatedShapesByVessel}};
  return manifest.dump();
}

ProjectState deserializeManifest(const std::string& manifestText) {
  json manifest = json::parse(manifestText);  // 解析失敗時は例外送出（呼び出し元でPersistenceErrorへ変換）

  ProjectState state;
  for (const auto& jj : manifest.at("fragmentSources")) state.fragmentSources.push_back(fragmentSourceRefFromJson(jj));
  state.unclassifiedFragmentIds = manifest.at("unclassifiedFragmentIds").get<std::vector<std::size_t>>();
  for (const auto& jj : manifest.at("assemblyStates")) state.assemblyStates.push_back(assemblyStateFromJson(jj));
  for (const auto& jVessel : manifest.at("estimatedShapesByVessel")) {
    std::vector<EstimatedShapeRecord> shapes;
    for (const auto& jj : jVessel) shapes.push_back(estimatedShapeRecordFromJson(jj));
    state.estimatedShapesByVessel.push_back(std::move(shapes));
  }
  return state;
}

// --- ZIPコンテナI/O（ADR-0004） ------------------------------------------

constexpr const char* kManifestEntryName = "manifest.json";

void writeZipWithManifest(const std::string& zipPath, const std::string& manifestText) {
  zipFile zf = zipOpen(zipPath.c_str(), APPEND_STATUS_CREATE);
  if (zf == nullptr) {
    throw PersistenceError(zipPath, "一時ファイルを作成できませんでした（書き込み権限またはディスク容量を確認してください）");
  }
  int rc = zipOpenNewFileInZip(zf, kManifestEntryName, nullptr, nullptr, 0, nullptr, 0, nullptr, Z_DEFLATED,
                                Z_DEFAULT_COMPRESSION);
  if (rc != ZIP_OK) {
    zipClose(zf, nullptr);
    throw PersistenceError(zipPath, "ZIPコンテナ内にマニフェストエントリを作成できませんでした");
  }
  rc = zipWriteInFileInZip(zf, manifestText.data(), static_cast<unsigned int>(manifestText.size()));
  if (rc != ZIP_OK) {
    zipCloseFileInZip(zf);
    zipClose(zf, nullptr);
    throw PersistenceError(zipPath, "マニフェスト内容の書き込みに失敗しました");
  }
  zipCloseFileInZip(zf);
  zipClose(zf, nullptr);
}

std::string readManifestFromZip(const std::string& zipPath) {
  unzFile uf = unzOpen(zipPath.c_str());
  if (uf == nullptr) {
    throw PersistenceError(zipPath, "プロジェクトファイルを開けませんでした（存在しないか、ZIPコンテナとして不正です）");
  }
  if (unzLocateFile(uf, kManifestEntryName, 0) != UNZ_OK) {
    unzClose(uf);
    throw PersistenceError(zipPath, "プロジェクトファイル内にマニフェストが見つかりません");
  }
  if (unzOpenCurrentFile(uf) != UNZ_OK) {
    unzClose(uf);
    throw PersistenceError(zipPath, "マニフェストエントリを開けませんでした");
  }
  std::string content;
  char buffer[8192];
  int bytesRead = 0;
  while ((bytesRead = unzReadCurrentFile(uf, buffer, sizeof(buffer))) > 0) {
    content.append(buffer, static_cast<std::size_t>(bytesRead));
  }
  bool readError = bytesRead < 0;
  unzCloseCurrentFile(uf);
  unzClose(uf);
  if (readError) {
    throw PersistenceError(zipPath, "マニフェスト内容の読み込みに失敗しました");
  }
  return content;
}

}  // namespace

FragmentSourceRef computeFragmentSourceRef(std::size_t fragmentId, const std::string& filePath) {
  return FragmentSourceRef{fragmentId, filePath, sha256HexOfFile(filePath)};
}

namespace {

// write-temp-then-renameの最終置換ステップ（REQ-POTTERY-023）。
// std::rename はPOSIX（Linux/macOS）では既存の宛先ファイルをアトミックに
// 置換するが、Windowsでは宛先が既に存在すると失敗する（REQ-POTTERY-017が
// 要求するWindowsデスクトップ環境での動作に反する）。そのためWindows上では
// MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) を
// 用いて既存ファイルの置換を行う。
bool atomicReplaceFile(const std::string& tempPath, const std::string& targetPath) {
#ifdef _WIN32
  std::wstring tempPathW(tempPath.begin(), tempPath.end());
  std::wstring targetPathW(targetPath.begin(), targetPath.end());
  return MoveFileExW(tempPathW.c_str(), targetPathW.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return std::rename(tempPath.c_str(), targetPath.c_str()) == 0;
#endif
}

}  // namespace

PersistenceError::PersistenceError(std::string targetIn, std::string reasonIn)
    : std::runtime_error(targetIn + ": " + reasonIn), target(std::move(targetIn)), reason(std::move(reasonIn)) {}

void saveProject(const std::string& path, const ProjectState& projectState,
                  std::function<void()> faultInjectionHookForTesting) {
  const std::string tempPath = path + ".tmp";
  const std::string manifestText = serializeManifest(projectState);

  // write-temp-then-rename（REQ-POTTERY-023）: 一時ファイルへ書き込み、
  // 完了後にアトミックなリネームで既存ファイルを置換する。呼び出し側から
  // 見た処理対象は常に path（一時ファイル名は内部実装の詳細）であるため、
  // 一時ファイル書き込み段階の失敗も path を対象として報告し直す。
  try {
    writeZipWithManifest(tempPath, manifestText);
  } catch (const PersistenceError& e) {
    throw PersistenceError(path, e.reason);
  }

  if (faultInjectionHookForTesting) {
    // リネーム前にテストが疑似的な内部エラーを注入できるフック
    // （REQ-POTTERY-021/023）。例外送出時は一時ファイルのみが残り、
    // path の既存ファイルは一切変更されない。
    faultInjectionHookForTesting();
  }

  if (!atomicReplaceFile(tempPath, path)) {
    std::remove(tempPath.c_str());
    throw PersistenceError(path, "一時ファイルから正式なプロジェクトファイルへの置換（リネーム）に失敗しました");
  }
}

ProjectState loadProject(const std::string& path) {
  const std::string manifestText = readManifestFromZip(path);

  ProjectState state;
  try {
    state = deserializeManifest(manifestText);
  } catch (const std::exception& e) {
    throw PersistenceError(path, std::string("マニフェストの解析に失敗しました: ") + e.what());
  }

  // 参照先スキャンデータファイルの欠落・内容変更を破片単位で検出する
  // （REQ-POTTERY-024）。
  for (const auto& ref : state.fragmentSources) {
    if (!fileExists(ref.filePath)) {
      state.integrityWarnings.push_back(IntegrityWarning{ref.fragmentId, ref.filePath, IntegrityIssue::Missing});
      continue;
    }
    const std::string currentHash = sha256HexOfFile(ref.filePath);
    if (currentHash != ref.sha256Hex) {
      state.integrityWarnings.push_back(IntegrityWarning{ref.fragmentId, ref.filePath, IntegrityIssue::HashMismatch});
    }
  }
  return state;
}

}  // namespace kintsugi::core
