#include "assembly_orchestrator.hpp"

#include <algorithm>

namespace kintsugi::core {

std::optional<PropagatedPose> AssemblyState::resolvedPose(std::size_t fragmentId) const {
  const auto overrideIt = manualPoseOverrides.find(fragmentId);
  if (overrideIt != manualPoseOverrides.end()) {
    return overrideIt->second;
  }
  for (const auto& join : acceptedJoins) {
    if (join.fragmentIdB == fragmentId) {
      return join.pose;
    }
  }
  // fragmentIdが接合の基準側（fragmentIdA）としてのみ使われている場合、
  // 採用時点でその破片は恒等姿勢（局所的な起点）として扱われている。
  for (const auto& join : acceptedJoins) {
    if (join.fragmentIdA == fragmentId) {
      return PropagatedPose{Vec3{0.0, 0.0, 0.0}, Quaternion{1.0, 0.0, 0.0, 0.0}};
    }
  }
  return std::nullopt;
}

bool AssemblyState::hasAcceptedJoin(std::size_t fragmentId) const {
  for (const auto& join : acceptedJoins) {
    if (join.fragmentIdA == fragmentId || join.fragmentIdB == fragmentId) {
      return true;
    }
  }
  return false;
}

AssemblyOrchestrator::AssemblyOrchestrator(std::vector<std::size_t> fragmentIds) {
  state_.fragmentIds = std::move(fragmentIds);
}

const AssemblyState& AssemblyOrchestrator::state() const { return state_; }

const AssemblyState& AssemblyOrchestrator::runFullAuto(const std::vector<JoinCandidate>& candidates) {
  snapshotBeforeMutation();

  std::vector<JoinCandidate> queue;
  for (const auto& candidate : candidates) {
    if (candidate.confidenceScore >= 70.0) {
      queue.push_back(candidate);
    }
  }
  std::stable_sort(queue.begin(), queue.end(), [](const JoinCandidate& lhs, const JoinCandidate& rhs) {
    if (lhs.confidenceScore != rhs.confidenceScore) {
      return lhs.confidenceScore > rhs.confidenceScore;
    }
    if (lhs.fragmentIdA != rhs.fragmentIdA) {
      return lhs.fragmentIdA < rhs.fragmentIdA;
    }
    return lhs.fragmentIdB < rhs.fragmentIdB;
  });

  for (const auto& candidate : queue) {
    const auto result = evaluateCandidate(candidate);
    if (std::holds_alternative<ConflictWarning>(result)) {
      RejectedCandidate rejected;
      rejected.fragmentIdA = candidate.fragmentIdA;
      rejected.fragmentIdB = candidate.fragmentIdB;
      rejected.confidenceScore = candidate.confidenceScore;
      rejected.reason = RejectionReason::Conflict;
      state_.rejectedCandidates.push_back(rejected);
    } else {
      state_.acceptedJoins.push_back(std::get<AcceptedJoin>(result));
    }
  }
  return state_;
}

CandidateListView AssemblyOrchestrator::presentCandidates(const std::vector<JoinCandidate>& candidates) const {
  std::vector<JoinCandidate> sorted = candidates;
  std::stable_sort(sorted.begin(), sorted.end(), [](const JoinCandidate& lhs, const JoinCandidate& rhs) {
    if (lhs.confidenceScore != rhs.confidenceScore) {
      return lhs.confidenceScore > rhs.confidenceScore;
    }
    if (lhs.fragmentIdA != rhs.fragmentIdA) {
      return lhs.fragmentIdA < rhs.fragmentIdA;
    }
    return lhs.fragmentIdB < rhs.fragmentIdB;
  });
  return CandidateListView{std::move(sorted)};
}

AcceptCandidateResult AssemblyOrchestrator::acceptCandidate(const JoinCandidate& candidate) {
  auto result = evaluateCandidate(candidate);
  if (std::holds_alternative<ConflictWarning>(result)) {
    return std::get<ConflictWarning>(result);
  }
  snapshotBeforeMutation();
  state_.acceptedJoins.push_back(std::get<AcceptedJoin>(result));
  return state_;
}

const AssemblyState& AssemblyOrchestrator::rejectCandidate(const JoinCandidate& candidate) {
  snapshotBeforeMutation();
  RejectedCandidate rejected;
  rejected.fragmentIdA = candidate.fragmentIdA;
  rejected.fragmentIdB = candidate.fragmentIdB;
  rejected.confidenceScore = candidate.confidenceScore;
  rejected.reason = RejectionReason::UserRejected;
  state_.rejectedCandidates.push_back(rejected);
  return state_;
}

const AssemblyState& AssemblyOrchestrator::applyManualTransform(std::size_t fragmentId,
                                                                 const PropagatedPose& transform) {
  snapshotBeforeMutation();
  state_.manualPoseOverrides[fragmentId] = transform;
  return state_;
}

const AssemblyState& AssemblyOrchestrator::undo() {
  if (undoStack_.empty()) {
    return state_;
  }
  redoStack_.push_back(state_);
  state_ = undoStack_.back();
  undoStack_.pop_back();
  return state_;
}

const AssemblyState& AssemblyOrchestrator::redo() {
  if (redoStack_.empty()) {
    return state_;
  }
  undoStack_.push_back(state_);
  state_ = redoStack_.back();
  redoStack_.pop_back();
  return state_;
}

void AssemblyOrchestrator::snapshotBeforeMutation() {
  undoStack_.push_back(state_);
  redoStack_.clear();
}

std::variant<AcceptedJoin, ConflictWarning> AssemblyOrchestrator::evaluateCandidate(
    const JoinCandidate& candidate) const {
  FragmentAnchorMap anchors;
  if (const auto anchorA = state_.resolvedPose(candidate.fragmentIdA)) {
    anchors[candidate.fragmentIdA] = *anchorA;
  }
  const PropagatedPose proposedB = propagatePose(candidate, anchors);

  if (const auto existingB = state_.resolvedPose(candidate.fragmentIdB)) {
    if (isConflicting(proposedB, *existingB)) {
      ConflictWarning warning;
      warning.fragmentIdA = candidate.fragmentIdA;
      warning.fragmentIdB = candidate.fragmentIdB;
      warning.proposedPose = proposedB;
      warning.establishedPose = *existingB;
      return warning;
    }
  }

  AcceptedJoin join;
  join.fragmentIdA = candidate.fragmentIdA;
  join.fragmentIdB = candidate.fragmentIdB;
  join.pose = proposedB;
  join.confidenceScore = candidate.confidenceScore;
  return join;
}

}  // namespace kintsugi::core
