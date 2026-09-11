---
name: sdd-implementation
description: "Use when implementing approved SDD requirements and designs with explicit code/test trace annotations and real verification evidence. 設計に基づく実装・テスト時に使用。"
---
# Implementation / 実装

Follow the user's input language (日本語 / English). Use Copilot's native planning,
editing and subagents for implementation; do not introduce code generators,
generic test generators or a second orchestration/task system.
Run only the repository's exact `musubix3` CLI. Never fall back to similarly
named npm packages; report a blocker if the executable is unavailable.
After the work, run `npx musubix3 workflow-record sdd-implementation complete
--status completed` exactly once.

1. Before editing implementation code, verify that approved requirements and
   design artifacts exist, both validators pass, and `approval validate` reports
   current requirements and design approval. If either is absent, stale, or invalid,
   stop and return to `sdd-change`, invoking `sdd-requirements` and `sdd-design`
   as needed. `tdd red` also enforces design approval. Do not infer approval from
   validation or a natural-language request such as
   "develop", "build", 「開発」, 「作成」, or 「実装」.
2. For behavior changes and bug fixes, write a meaningful failing test before
   implementation. Configure the selected command with `tddArgs` containing
   `{testId}` or `{testPath}` and a `tddReport`, or use a built-in runner adapter.
   Follow its native identity contract: pytest/Cargo use underscore names, Go
   uses a `TEST-*` subtest, JUnit uses an exact `@Tag("TEST-*")`, and xUnit uses
   a `Fact` `DisplayName` containing the exact ID.
   Run the repository formatter before recording Red. Formatting is part of the
   test fingerprint: after Red, do not edit or reformat the authoritative test
   until the matching Green has been recorded.
   Make the runner emit a fresh
   `musubix-json` report containing only the selected test (plus declared
   deterministic `operations` counters when applicable) and run
   `npx musubix3 tdd red <TEST-ID> --requirement <REQ-ID> --command <name>`.
3. Implement only enough code to pass, preserving the test unchanged, then run
   `tdd green`. Refactor only after Green and record `tdd refactor`.
   Use `tdd validate` to inspect persisted order, fingerprints, durations and
   hash-chain evidence before claiming the cycle is complete.
4. Add one block comment per trace entity:
   ```ts
   /** @id CODE-FEATURE-001
    * @implements REQ-FEATURE-001
    * @design DES-FEATURE-001
    */
   ```
   Tests use a separate block:
   ```ts
   /** @id TEST-FEATURE-001
    * @verifies REQ-FEATURE-001
    */
   ```
   IDs are globally unique. Multiple targets are comma/space-separated.
   Put annotations in the authoritative source and test files, including
   supported non-JS/TS files. In Python, use consecutive `#` comment lines;
   annotations inside docstrings are ignored and diagnosed. Never add a proxy file just to increase coverage.
   An annotation establishes a link, not proof that code or tests are correct.
5. Before recording Red, run every configured `tdd.redPreflightCommands`
   formatter/check and let musubix3 enforce that preflight. Then run the
   project's actual focused tests/typecheck/build; fix failures.
   When mutation evidence is required, use the project's existing lightweight
   mutation mechanism to emit schema-v1 deterministic identities, requirement/
   test linkage, source/test SHA-256, operator/location, and killed status.
   Never fabricate results or add a large mutation dependency. Use
   `npx musubix3 mutation doctor --json` to inspect locally available engines
   and configuration recommendations. Before Python mutation runs, remove
   existing `__pycache__` directories, then use `-B` for mutation and test
   commands so stale bytecode is absent and no new `.pyc` files are created.
6. Regenerate `npx musubix3 trace build`, check `trace check --strict`, then
   `npx musubix3 gate --changed`. Configure real command/argument arrays first.
7. Use Copilot's native review and security-review capabilities when appropriate.
   Report executed evidence separately from review advice and skipped work.
