---
name: sdd-quality
description: "Use when deciding release readiness from actual checks, measurable policy, architecture and trace evidence, including incremental change checks. 品質ゲート・リリース判定時に使用。"
---
# Quality / 品質
Follow the user's input language. Native review/security review remain separate.
After the work, run `npx musubix3 workflow-record sdd-quality complete --status
completed` exactly once.
1. Inspect config/baseline first. Execute only trusted argument-array commands;
   never edit baseline without independent approval or substitute another CLI.
2. Configure real tests/build/typecheck commands and timeouts. Use an explicit
   custom report or a built-in Vitest/Jest, pytest, Go test, Cargo, JUnit or .NET
   adapter. Require executable native adapter contracts in CI; JUnit targets use
   an exact `@Tag("TEST-*")`, and pytest requires `pytest-json-report`. Review
   `requiredChecks`, `qualityProfile`, coverage thresholds and architecture rules.
   Use `recommended` or `release` only when all profile requirements are
   intentionally configured; profile validation never invents missing evidence. Use strict
   `codeGraph.mode` when unresolved computed module loading must block release,
   and ensure the trusted baseline prevents downgrading it. Configure a
   fresh structured `testReport` for `test-identities`. Do not weaken policy.
3. Run `npx musubix3 gate --json` or `npx musubix3 gate --changed --json`.
   `evidence refresh --json` runs the same fail-closed pipeline. If `input-stability` fails,
   inspect its per-path added/modified/deleted diagnostics and stop generators
   or formatters before rerunning. Standard dependency/build directories,
   including manifest-scoped Cargo/Maven `target/`, .NET `bin`/`obj`, project-local
   `.nuget/packages`, and `.venv` or `venv` roots with a regular `pyvenv.cfg`, are excluded; arbitrary source
   directories and source-like generated inputs are not silently ignored.
   Changed mode reports Git changes and dependent files but conservatively runs
   all checks, including commands. It never treats unrun checks as successful.
   When `tdd` is required, confirm every command has test-scoped `tddArgs`, every
   cycle has a fresh structured `tddReport`, and a real failing Red is followed
   by a passing Green using the same command and unchanged test.
4. Inspect `.musubix/evidence/quality.json`: pass/fail/skipped, required flags,
   command exits/output, measured rules, preserved changed-run context and input
   fingerprints. Inspect `formal.json` and `workflow.json` when configured.
   Treat workflow records as declarations until `workflow-verify` binds each
   completed declaration to one distinct completed Copilot Skill tool call.
   Use `workflow-sanitize` before review; it validates before filtering.
   In strict mode require one final successful result, matching session UUID,
   causal transcript order, policy-bounded concurrent-event clock skew, bounded
   freshness, complete tool lifecycles and canonical transcript hash. Ensure the
   baseline protects strict/session/freshness/event-skew policy.
   When `change-history` is required,
   confirm ordered checkpoints, chained TDD records, requirement-scoped Code
   Graph changes, exact CHANGE requirement enumeration, measurable Acceptance,
   concrete designs, authoritative tests and per-CHANGE artifact completeness.
   Enforce declared operation counters for deterministic performance budgets;
   wall-clock duration alone is insufficient. Confirm each observation's hashed
   gate run, exact command, fresh report, passing test, counter and exit provenance;
   duplicate sources, persisted stdout report changes and configuration drift
   invalidate it. Equivalent reruns may change provenance IDs without changing
   the signed semantic performance head.
   For explicit `Formal:` JSON, require model-correspondence evidence binding
   the current model and generated trace to an authoritative `TEST-*` passed by
   a fresh structured report. For release, set `mutation.mode` to `strict`,
   protect it and its command in the baseline, and require deterministic mutant
   identities bound to must functional requirements, current source/test
   fingerprints, operator/location, authoritative tests, and killed results.
   Reject survived, skipped, duplicate, conflicting, stale, or unlinked mutants.
   Treat zero executed or non-passing tests as incomplete evidence after exit zero; run `mutation doctor`;
   musubix3 validates evidence and does not bundle a mutation engine.
   Missing required tools, commands, artifacts or evidence block readiness.
   A candidate gate may fail only because release approval is missing; that is
   not final readiness and must not be described as pass.
5. Record native review/security findings separately; never fabricate evidence.
6. For static CI provenance, configure trusted Ed25519 public keys, freshness
   bounds and `ci-required`; sign `attestation payload` outside musubix3. For
   GitHub Actions identity, opt into strict `githubOidc`, derive the key-bound
   custom audience, obtain a short-lived token, and include only the public key
   and token in the payload. Verification must fetch issuer metadata/JWKS and
   check all configured claims; offline strict verification fails closed.
   Ensure the baseline protects CI-required mode, strict OIDC and key binding.
   Never store a private key or overstate OIDC as proof of arbitrary runner work.
7. Before requesting human approval, run Copilot's native `rubber-duck` review agent on the release evidence summary and the CHANGE document; fix every reported issue, then re-run the review, repeating until it reports zero remaining issues.
8. After required non-approval checks pass, run `approval prepare release`, show its exact hash and residual risks, ask one approve/reject question, then wait. On approval only record that hash with `approval record release --approver <name> --artifact-sha256 <hash> --confirm`; rejection stops. Rerun gate/status; stale approval blocks commit/push/publish/deploy; no resident watcher or REPL.
