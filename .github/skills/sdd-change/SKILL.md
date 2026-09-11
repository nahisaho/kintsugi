---
name: sdd-change
description: "Use as the MANDATORY first Skill for requests to develop, build, create, implement, add, change, or fix software, including 「開発」「作成」「実装」. Start requirements and design before code, then propagate through tests, traceability, and quality evidence. ソフトウェア開発依頼では実装前に必ず要求定義・設計から開始。"
---
# Integrated change workflow / 統合変更ワークフロー
/* @id CODE-SESSION-SCOPED-DEVELOPMENT-001
 * @implements REQ-SESSION-SCOPED-DEVELOPMENT-001 REQ-SESSION-SCOPED-DEVELOPMENT-002
 * @design DES-SESSION-SCOPED-DEVELOPMENT-001
 */
Mandatory entrypoint: every new natural-language development request is a new change, even in an existing Copilot session; never reuse prior requirements, approvals, TDD, or change evidence unless the user explicitly names the existing change ID and asks to continue it. never start implementation before validating requirements/design; skip only for verified approved artifacts of that explicitly continued change.
Never infer approval; show `approval prepare <stage>` and record only its reviewed hash with `approval record <stage> --approver <name> --artifact-sha256 <hash> --confirm`.
Whenever an AI deliverable is documentation (requirements, design, ADRs, the CHANGE document, or release/quality evidence), run Copilot's native `rubber-duck` review agent on it before that phase's human approval, fixing every issue and re-reviewing until none remain.
Follow the user's input language. Use native Copilot planning, editing, research, review, security review and subagents.
Record exactly one final invocation outcome with `npx musubix3 workflow-record sdd-change complete --status <status>`; `change-record` separately proves phases.
Run `workflow-sanitize <copilot.jsonl> <safe.jsonl>` before review, then
`workflow-verify <safe.jsonl>`; it validates source-order lifecycles without
assuming globally monotonic clocks unless `maxEventSkewMs` is explicitly set.
Baseline-protect transcript byte limits; never truncate/edit to bypass them.
For strict evidence, bind an expected UUID; GitHub origin needs strict OIDC.
Never record multiple declarations per invocation; use only the configured CLI.
For broad work, use short stages: initialize, requirements, requirements approval, design, design approval, real Red, Green, integration, trace/formal, quality, release approval. Report each result before the next prompt.
For a staged change, run `change-record <CHANGE-ID> <phase> --requirement <REQ-ID...>` after each phase in this exact order: `impact`, `requirements`, `design`, `red`, `implementation`, `green`, `quality`.
`impact`/`requirements`/`design`/`quality` always use the full requirement ID set; `red`/`implementation`/`green` may instead use a non-empty subset as an independent per-requirement batch for an interleaved Red-Implementation-Green loop; `quality` still needs full cumulative Green coverage.
List only requirements whose statement/acceptance changes, classify each, and
document other impacts separately. Each needs fresh Red and Green.
The CHANGE document must contain `Requirements:` with exactly those normative IDs.
Persisted monotonic order, not wall-clock time, proves these phase boundaries.
## 1. Classify and inspect / 分類と事前確認
1. Classify the request as a feature, behavior change, defect correction,
   refactoring, or documentation-only change. For a new program/feature, create
   a fresh feature slug and CHANGE artifact; prior session context is not approval.
2. Read the constitution and relevant requirements, designs, ADRs, code and tests, but treat prior-session artifacts as historical context unless continuation is explicit.
3. Run `trace impact`; when code exists run `graph index` and `graph impact`.
4. Separate confirmed intent, assumptions and open questions. When material context is missing, ask exactly one highest-priority question, wait, then repeat; never batch questions or finalize requirements, design or code while blockers remain.
## 2. Update specifications first / 仕様を先に更新
1. For new or changed observable behavior, add or revise EARS requirements and
   measurable acceptance criteria before implementation. Preserve stable IDs
   when meaning remains the same; create new IDs when obligations are distinct.
2. For a bug where implementation violates an existing requirement, keep that
   requirement and record that no specification change is needed. Never rewrite
   a requirement merely to make incorrect behavior appear compliant.
3. Run requirements and constitution validation; stop on invalid artifacts
   instead of continuing with unapproved assumptions.
4. Run a `rubber-duck` review (Copilot's native review agent) of `requirements.md`, fixing every issue and re-reviewing until none remain, then stop for explicit current `requirements` approval before design. An edit to `requirements.md` re-opens approval and requires re-review.
5. Update design responsibilities/interfaces/constraints/links and ADRs, run design validation, then run the same rubber-duck review/fix loop on `design.md`/ADRs before stopping for explicit current `design` approval before Red/implementation. An edit re-opens approval and requires re-review.
## 3. Implement and prove coverage / 実装と網羅性
1. For observable behavior changes and defect fixes, write the smallest meaningful
   test first. Include its `TEST-*` ID in the test name/output and link it to the
   requirement with `@verifies`. Configure the command's `tddArgs` with
   `{testId}` or `{testPath}` so only that test is selected. Configure
   `tddReport` and make the runner write a fresh `musubix-json` result containing
   exactly the target test with `failed` or `passed` status, or select a built-in
   test adapter. For deterministic performance requirements, use a passing
   instrumented `operations` report with command/report/run/exit provenance;
   native adapters cannot emit app counters, and elapsed time is insufficient.
2. Run `npx musubix3 tdd red <TEST-ID> --requirement <REQ-ID> --command <name>`.
   Do not edit implementation code until this records the expected failing test.
3. Implement the smallest complete change, preserving the test, then run
   `tdd green` with the same IDs and command. Refactor only after Green and record
   `tdd refactor` after the refactored code passes.
4. Maintain unique IDs and trace annotations in authoritative files; never add
   proxies for coverage. Links locate evidence, not proof.
5. Run focused tests, then configured typecheck, build and complete test commands.
Documentation/prototypes may omit TDD only when policy allows; record the reason.
## 4. Rebuild evidence and finish / 根拠更新と完了
1. Run `trace build`, `trace check --strict`, `graph index`, and `graph gate`.
2. Run `formal check` when changed requirements fit its documented abstraction;
   use strict `Formal:` JSON for explicit conditional, numeric, temporal or
   transition semantics; report unsupported prose rather than claiming proof.
   A required `formal` check enforces modeled fraction and configured solver.
3. Run `gate --changed --json` and `status --json`. Repair failures, dangling
   links and stale evidence; never weaken requirements or policy to obtain green.
4. Treat the first otherwise-passing gate as the release candidate. Run a `rubber-duck` review of the release/quality evidence summary and the CHANGE document; fix every reported issue and re-review until zero issues remain. Before any release operation, prepare/show its exact hash, ask one human approve/reject question and wait; record only that hash, then rerun gate/status.
5. Complete only when required checks pass and `status.gate.ready` is true;
   otherwise report blockers. One-phase work must state downstream work.
