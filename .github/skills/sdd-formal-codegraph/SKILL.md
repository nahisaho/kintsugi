---
name: sdd-formal-codegraph
description: "Use for optional formal/neurosymbolic consistency checks, compiler dependency graphs, architecture cycles and conservative code impact analysis. 形式的整合性・コードグラフ・影響分析時に使用。"
---
# Formal checks and codegraph / 形式手法・コードグラフ

Follow the user's input language (日本語 / English). Use native Copilot reasoning,
editing and code navigation to propose and refine artifacts. Deterministic checks
constrain these proposals; do not create a separate AI/agent runtime.
After the work, run `npx musubix3 workflow-record sdd-formal-codegraph complete
--status completed` exactly once.

## Formal consistency / 形式的整合性
1. Run `npx musubix3 formal generate <requirements-file> --format both --json`
   to inspect reproducible SMT-LIB2 and Lean artifacts before execution.
2. Run `npx musubix3 formal doctor --json`, then
   `npx musubix3 formal check <requirements-file> --solver none --json`.
   Controlled unconditional English/Japanese obligations enter the Boolean
   abstraction. Strict optional `Formal:` JSON adds explicit conditional truth,
   exact integer bounds (`ms`/`s`/`min`, `bytes`/`kib`/`mib`), intersected
   optional `afterMs`/required `withinMs` response windows, and deterministic
   state transitions. Other units and incompatible dimensions stay separate.
3. Inspect `unsupported`, assumptions and diagnostics. Arbitrary prose, domain
   axioms, scheduling and liveness are not proved. A partial consistency result
   says nothing about unmodeled requirements or implementation correctness.
4. Optionally use `--solver auto|z3|lean`. Z3 checks named QF_UFLIA assertions;
   Lean checks explicit-witness Boolean and translated constraint theorems.
   Use `--z3-command`, `--lean-command`, or the matching `MUSUBIX3_*`
   environment variable for nonstandard installations. Missing,
   unknown, timed-out or failed solvers are explicit, never proof success.
5. Resolve contradictions with native planning/editing; retain domain judgment.
6. For explicit `Formal:` JSON, run the full gate and
   `model-correspondence validate`. The current model and generated trace must
   reach an authoritative test passed in a fresh structured command report.

## Code impact / コード影響
1. Run `npx musubix3 graph index` (or `graph index --changed`).
2. Run `graph impact <symbol-or-path>`, `graph cycles`, and `graph gate --json`.
   Use `path#symbol` to disambiguate names. Reverse-import paths explain impact.
3. Rules in config specify source and disallowed target globs; cycles use SCCs.
   JS/TS imports, re-exports, literal require/dynamic imports, manifest
   entrypoints and local cache-busting URL/templates with a static base are indexed.
   Set `codeGraph.mode` to `strict` when unresolved computed `import()` or
   `require()` calls, including computed PHP include/require expressions, must
   block graph gates; the compatible default warns.
   Conservative native adapters index JS/TS, Rust, Python, Go, Java, Kotlin,
   C/C++, Objective-C, C#/.NET, Ruby, PHP, Swift, Dart, Scala, Elixir, Haskell,
   Lua, Zig, Solidity, R and Julia dependencies, declarations and direct calls.
   Unrecognized extensions stay outside graph inputs.
   Statically resolved cache-busting imports remain valid in strict mode.
   Reflective behavior remains an explicit limitation.
4. Use native navigation and review to confirm conservative results; rebuild
   stale caches. No custom LSP/MCP management, generic review or daemon.
