---
name: sdd-requirements
description: "Use when eliciting, refining, or validating EARS requirements and measurable constitution rules for specification-driven development. 要求定義・EARS検証・憲章策定に使用。"
---
# Requirements / 要求
/* @id CODE-SESSION-SCOPED-DEVELOPMENT-002
 * @implements REQ-SESSION-SCOPED-DEVELOPMENT-001 REQ-SESSION-SCOPED-DEVELOPMENT-002
 * @design DES-SESSION-SCOPED-DEVELOPMENT-002
 */
Respond and generate guidance in the user's input language (日本語 / English).
Use Copilot's native planning, questions, research and editing; do not create an
interview engine or research agent framework.
After the work, run `npx musubix3 workflow-record sdd-requirements complete
--status completed` exactly once.

1. For every new natural-language development request, create/use a fresh
   `.musubix/features/<new-slug>/requirements.md`; do not edit or reuse a prior
   session's feature, approval, or change evidence unless continuation is explicit.
   Inspect `.musubix/constitution.md` and relevant existing requirements only as
   context. If absent, preview `npx musubix3 init --dry-run` before installing.
2. Treat a request such as 「○○を開発」/"develop ○○" as requirements elicitation,
   not permission to start coding. Use native planning to clarify scope, stakeholders, measurable acceptance and
   failure behavior. Separate assumptions from confirmed requirements. If required
   context is missing, use native elicitation to ask exactly one highest-priority
   question and wait for the answer before asking the next. Never batch questions.
   Repeat until no material blocker remains; do not finalize requirements earlier.
3. Edit `.musubix/features/<slug>/requirements.md` with headings
   `## REQ-FEATURE-001: Title`, `Priority: must|should|may`,
   `Type: functional|non-functional`, and `Statement: ...`. IDs are globally
   unique. Keep one obligation per entry; add `Acceptance: ...`.
   Acceptance must be non-placeholder and measurably testable. For semantics
   that must enter formal coverage, add strict one-line `Formal:` JSON using
   branch-scoped `conditional` (`condition`/`consequence`), integer `numeric`
   (`metric`/`operator` one of `<`,`<=`,`=`,`>=`,`>`/`value`), `temporal`
   (`trigger`/`response`/required `withinMs`/optional nonnegative `afterMs`), or
   deterministic `transition` (`from`/`event`/`to`).
   For numeric constraints, use compatible duration (`ms`/`s`/`min`) or size
   (`bytes`/`kib`/`mib`) units when conversion is intended; never translate
   arbitrary prose or incompatible dimensions by guesswork. A non-functional deterministic
   budget uses strict one-line `Performance:` JSON with `counter`, integer `max`, and `testId`.
4. Use all six controlled EARS forms as appropriate:
   - The system shall respond.
   - When an event occurs, the system shall respond.
   - While a state holds, the system shall respond.
   - If a fault occurs, then the system shall respond.
   - Where a feature is enabled, the system shall respond.
   - While a state holds, when an event occurs, the system shall respond.
   Japanese equivalents use `システムは…しなければならない。`, with
   `…とき、` / `…間、` / `もし…ならば、` / `…場合、` clauses.
5. Validate with `npx musubix3 requirements validate <file> --json` and
   `npx musubix3 constitution validate --json`. Rules use `PRINC-001`, `RULE-001`,
   a supported `Metric:` and numeric `Limit:`. Validation is not execution evidence.
6. Before requesting human approval, run Copilot's native `rubber-duck`
   review agent on `requirements.md`. Fix every reported issue, then re-run
   the review. Repeat fix-then-review until the review reports zero
   remaining issues; only then proceed to step 7. Do not request human
   approval while rubber-duck findings remain open.
7. Validation is not human approval. Before design, run `approval prepare
   requirements`, show its exact artifacts/hash, ask one explicit approve/reject
   question, then wait. On approval only, record that hash with `approval record
   requirements --approver <name> --artifact-sha256 <hash> --confirm`; rejection
   or any intervening artifact change stops and requires renewed review.
8. Hand off confirmed IDs and unresolved assumptions to `sdd-design`; use native
   review for semantic completeness, not merely syntactic conformance.
