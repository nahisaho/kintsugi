#include "gui_command_dispatcher.hpp"

namespace kintsugi::gui {

using kintsugi::core::AcceptCandidateResult;
using kintsugi::core::AssemblyOrchestrator;
using kintsugi::core::AssemblyState;
using kintsugi::core::ConflictWarning;
using kintsugi::core::JoinCandidate;
using kintsugi::core::PropagatedPose;

GuiCommandDispatcher::GuiCommandDispatcher(AssemblyOrchestrator& orchestrator) : orchestrator_(orchestrator) {}

const AssemblyState& GuiCommandDispatcher::acceptCandidate(const JoinCandidate& candidate) {
  const AcceptCandidateResult result = orchestrator_.acceptCandidate(candidate);
  if (const auto* warning = std::get_if<ConflictWarning>(&result)) {
    lastConflictWarning_ = *warning;
  } else {
    lastConflictWarning_ = std::nullopt;
  }
  return orchestrator_.state();
}

const AssemblyState& GuiCommandDispatcher::rejectCandidate(const JoinCandidate& candidate) {
  return orchestrator_.rejectCandidate(candidate);
}

const AssemblyState& GuiCommandDispatcher::applyManualDrag(std::size_t fragmentId,
                                                            const PropagatedPose& transform) {
  return orchestrator_.applyManualTransform(fragmentId, transform);
}

const AssemblyState& GuiCommandDispatcher::undo() { return orchestrator_.undo(); }

const AssemblyState& GuiCommandDispatcher::redo() { return orchestrator_.redo(); }

const std::optional<ConflictWarning>& GuiCommandDispatcher::lastConflictWarning() const {
  return lastConflictWarning_;
}

}  // namespace kintsugi::gui
