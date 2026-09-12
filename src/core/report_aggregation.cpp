#include "report_aggregation.hpp"

namespace kintsugi::core {

UnmatchedReport buildUnmatchedFragmentReport(const std::vector<AssemblyState>& assemblyStates,
                                              const std::vector<FragmentMesh>& unclassified) {
  UnmatchedReport report;
  for (std::size_t clusterIndex = 0; clusterIndex < assemblyStates.size(); ++clusterIndex) {
    const AssemblyState& state = assemblyStates[clusterIndex];
    for (const std::size_t fragmentId : state.fragmentIds) {
      if (!state.hasAcceptedJoin(fragmentId)) {
        report.unmatchedFragments.push_back(FragmentReference{clusterIndex, fragmentId});
      }
    }
  }
  for (std::size_t fragmentId = 0; fragmentId < unclassified.size(); ++fragmentId) {
    report.unmatchedFragments.push_back(FragmentReference{std::nullopt, fragmentId});
  }
  return report;
}

RejectedCandidateReport buildRejectedCandidateReport(const std::vector<AssemblyState>& assemblyStates) {
  RejectedCandidateReport report;
  for (std::size_t clusterIndex = 0; clusterIndex < assemblyStates.size(); ++clusterIndex) {
    const AssemblyState& state = assemblyStates[clusterIndex];
    for (const RejectedCandidate& rejected : state.rejectedCandidates) {
      report.rejectedCandidates.push_back(RejectedCandidateEntry{
          clusterIndex, rejected.fragmentIdA, rejected.fragmentIdB, rejected.confidenceScore, rejected.reason});
    }
  }
  return report;
}

JoinReport buildJoinReport(const std::vector<AssemblyState>& assemblyStates) {
  JoinReport report;
  for (std::size_t clusterIndex = 0; clusterIndex < assemblyStates.size(); ++clusterIndex) {
    const AssemblyState& state = assemblyStates[clusterIndex];
    for (const AcceptedJoin& join : state.acceptedJoins) {
      report.joins.push_back(
          JoinReportEntry{clusterIndex, join.fragmentIdA, join.fragmentIdB, join.confidenceScore});
    }
  }
  return report;
}

PoseExportData buildPoseExport(const std::vector<AssemblyState>& assemblyStates,
                                const std::vector<FragmentMesh>& unclassified) {
  static const PropagatedPose kIdentityPose{Vec3{0.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};

  PoseExportData exportData;
  for (std::size_t clusterIndex = 0; clusterIndex < assemblyStates.size(); ++clusterIndex) {
    const AssemblyState& state = assemblyStates[clusterIndex];
    for (const std::size_t fragmentId : state.fragmentIds) {
      // joinState は buildUnmatchedFragmentReport と同一の判定基準
      // （hasAcceptedJoin）を用い、両レポート間で分類が食い違わないようにする。
      const JoinState joinState =
          state.hasAcceptedJoin(fragmentId) ? JoinState::Confirmed : JoinState::Unmatched;
      const PropagatedPose pose = state.resolvedPose(fragmentId).value_or(kIdentityPose);
      exportData.fragments.push_back(
          PoseExportEntry{FragmentReference{clusterIndex, fragmentId}, pose, joinState});
    }
  }
  for (std::size_t fragmentId = 0; fragmentId < unclassified.size(); ++fragmentId) {
    exportData.fragments.push_back(
        PoseExportEntry{FragmentReference{std::nullopt, fragmentId}, kIdentityPose, JoinState::Unmatched});
  }
  return exportData;
}

}  // namespace kintsugi::core
