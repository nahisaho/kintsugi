---
name: sdd-traceability
description: "Use when checking requirements-to-design/code/test coverage, dangling IDs, stale trace sources, or bidirectional change impact. 追跡可能性・網羅性・変更影響の確認時に使用。"
---
# Traceability / 追跡可能性

Follow the user's input language (日本語 / English). Use Copilot's native code
navigation, planning and editing to repair missing links; no custom LSP manager.
After the work, run `npx musubix3 workflow-record sdd-traceability complete
--status completed` exactly once.

1. Run `npx musubix3 trace build`. It scans `.musubix` artifacts and comment
   annotations in every Code Graph language, including JS/TS, Rust, Python, Go,
   JVM, C/C++, .NET, Ruby, PHP, Swift, Dart, BEAM, Haskell, Lua, Zig, Solidity,
   R and Julia, then generates feature `trace.json` files plus an ignored cache.
   Haskell `--`/`{- ... -}`, Lua `--`/`--[[ ... ]]`, and Visual Basic
   apostrophe/XML-documentation comments are scanned without accepting string
   literals as trace annotations.
2. Run `npx musubix3 trace check --strict --json`.
   Distinguish malformed IDs, dangling endpoints, stale inputs, and missing
   mandatory design/implementation/test coverage. Non-strict coverage warnings
   are not a release gate pass.
3. Use `npx musubix3 trace impact REQ-FEATURE-001 --json` (or a source path).
   Explain each result with its returned traversal path. Bidirectional
   connectivity is conservative, not a prediction of behavioral impact.
4. Use `sdd-formal-codegraph` for import/call evidence beyond explicit traces.
   Explicit Formal JSON additionally requires model-correspondence validation:
   generated trace evidence must reach an authoritative passing test.
   Let native planning/editing make substantive repairs; never add annotations
   or proxy source files solely to disguise unimplemented or untested requirements.
5. Rebuild after any source/artifact change. Never maintain trace JSON by hand.
   Use `npx musubix3 status` for one-shot readiness; do not start a watcher.
