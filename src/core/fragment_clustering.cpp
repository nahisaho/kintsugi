#include "fragment_clustering.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace kintsugi::core {

namespace {

// クラスタ所属信頼度の採用閾値（REQ-POTTERY-003 で70に固定）。
constexpr double kMembershipThreshold = 70.0;
// 肉厚差の正規化スケール（mm）。合成ベンチマークの器物間肉厚差を分離できる値。
constexpr double kThicknessScaleMm = 20.0;
// RGB色空間における最大ユークリッド距離（sqrt(255^2*3)）。
constexpr double kMaxColorDistance = 441.672943;

struct FragmentFeature {
  double thicknessMm = 0.0;
  bool hasColor = false;
  double meanR = 0.0;
  double meanG = 0.0;
  double meanB = 0.0;
};

// 破片の肉厚統計量（頂点座標の最小軸範囲）と頂点色平均を特徴量として抽出する。
FragmentFeature computeFeature(const FragmentMesh& fragment) {
  FragmentFeature feature;
  if (!fragment.vertices.empty()) {
    double minX = fragment.vertices.front().x, maxX = minX;
    double minY = fragment.vertices.front().y, maxY = minY;
    double minZ = fragment.vertices.front().z, maxZ = minZ;
    for (const auto& v : fragment.vertices) {
      minX = std::min(minX, v.x);
      maxX = std::max(maxX, v.x);
      minY = std::min(minY, v.y);
      maxY = std::max(maxY, v.y);
      minZ = std::min(minZ, v.z);
      maxZ = std::max(maxZ, v.z);
    }
    feature.thicknessMm = std::min({maxX - minX, maxY - minY, maxZ - minZ});
  }

  if (!fragment.colors.empty()) {
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    for (const auto& c : fragment.colors) {
      sumR += c.r;
      sumG += c.g;
      sumB += c.b;
    }
    const double n = static_cast<double>(fragment.colors.size());
    feature.meanR = sumR / n;
    feature.meanG = sumG / n;
    feature.meanB = sumB / n;
    feature.hasColor = true;
  }

  return feature;
}

// 2破片間の所属信頼度（0〜100、値が大きいほど同一器物由来の可能性が高い）を、
// 肉厚差と頂点色平均差の正規化距離から算出する（決定的処理）。
double pairwiseConfidence(const FragmentFeature& a, const FragmentFeature& b) {
  const double thicknessDist =
      std::min(std::abs(a.thicknessMm - b.thicknessMm) / kThicknessScaleMm, 1.0);

  double combinedDist = thicknessDist;
  if (a.hasColor && b.hasColor) {
    const double dr = a.meanR - b.meanR;
    const double dg = a.meanG - b.meanG;
    const double db = a.meanB - b.meanB;
    const double colorDist =
        std::min(std::sqrt(dr * dr + dg * dg + db * db) / kMaxColorDistance, 1.0);
    combinedDist = 0.5 * thicknessDist + 0.5 * colorDist;
  }

  const double confidence = (1.0 - combinedDist) * 100.0;
  return std::clamp(confidence, 0.0, 100.0);
}

// Union-Find（互いに素な集合）。破片インデックスの固定順走査により決定的にグループ化する。
class UnionFind {
 public:
  explicit UnionFind(std::size_t n) : parent_(n) {
    std::iota(parent_.begin(), parent_.end(), 0);
  }

  std::size_t find(std::size_t x) {
    while (parent_[x] != x) {
      parent_[x] = parent_[parent_[x]];
      x = parent_[x];
    }
    return x;
  }

  void unite(std::size_t a, std::size_t b) { parent_[find(a)] = find(b); }

 private:
  std::vector<std::size_t> parent_;
};

}  // namespace

ClusteringResult clusterFragments(const std::vector<FragmentMesh>& fragments) {
  const std::size_t n = fragments.size();
  std::vector<FragmentFeature> features;
  features.reserve(n);
  for (const auto& fragment : fragments) {
    features.push_back(computeFeature(fragment));
  }

  // 破片インデックスの昇順（固定順）でペア判定・結合を行い、決定的な結果を得る。
  UnionFind unionFind(n);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 1; j < n; ++j) {
      if (pairwiseConfidence(features[i], features[j]) >= kMembershipThreshold) {
        unionFind.unite(i, j);
      }
    }
  }

  // グループ（同一ルートを持つ破片の集合）へ分類する。
  std::vector<std::vector<std::size_t>> groupsByRoot(n);
  for (std::size_t i = 0; i < n; ++i) {
    groupsByRoot[unionFind.find(i)].push_back(i);
  }

  ClusteringResult result;
  std::vector<bool> isUnclassified(n, false);
  for (std::size_t root = 0; root < n; ++root) {
    auto members = groupsByRoot[root];
    if (members.size() < 2) {
      continue;
    }

    // Union-Findによる推移的併合は、A-B・B-Cがそれぞれ閾値以上でも
    // A-C間が閾値未満となるケースを許容してしまう（Issue #3）。
    // REQ-POTTERY-003は「所属信頼度70以上の破片のみをグルーピングし、
    // 70未満の破片は未分類として区別する」ことを要求するため、各破片の
    // グループ内平均信頼度が閾値未満になった時点でそのグループから除外し、
    // 安定するまで反復する（決定的な破片インデックス昇順で走査）。
    for (;;) {
      std::vector<double> memberConfidence(members.size(), 0.0);
      for (std::size_t k = 0; k < members.size(); ++k) {
        double sum = 0.0;
        for (std::size_t l = 0; l < members.size(); ++l) {
          if (l == k) continue;
          sum += pairwiseConfidence(features[members[k]], features[members[l]]);
        }
        memberConfidence[k] = sum / static_cast<double>(members.size() - 1);
      }

      std::vector<std::size_t> retained;
      for (std::size_t k = 0; k < members.size(); ++k) {
        if (memberConfidence[k] >= kMembershipThreshold) {
          retained.push_back(members[k]);
        }
      }

      if (retained.size() == members.size()) {
        break;  // 安定：全メンバーが閾値を満たす。
      }
      members = std::move(retained);
      if (members.size() < 2) {
        break;
      }
    }

    if (members.size() < 2) {
      for (std::size_t index : members) {
        isUnclassified[index] = true;
      }
      continue;
    }

    VesselCluster cluster;
    for (std::size_t index : members) {
      cluster.fragments.push_back(fragments[index]);
    }
    // 所属信頼度は、同一クラスタ内の他破片との信頼度平均とする。
    for (std::size_t index : members) {
      double sum = 0.0;
      for (std::size_t other : members) {
        if (other == index) continue;
        sum += pairwiseConfidence(features[index], features[other]);
      }
      cluster.membershipConfidence.push_back(sum / static_cast<double>(members.size() - 1));
    }
    result.vesselCandidates.push_back(std::move(cluster));
  }

  for (std::size_t root = 0; root < n; ++root) {
    if (groupsByRoot[root].size() == 1) {
      isUnclassified[groupsByRoot[root].front()] = true;
    }
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (isUnclassified[i]) {
      result.unclassified.push_back(fragments[i]);
    }
  }

  return result;
}

}  // namespace kintsugi::core
