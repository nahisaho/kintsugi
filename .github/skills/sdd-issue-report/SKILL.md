---
name: sdd-issue-report
description: "Use when a defect, bug, or gap is found (during development, testing, or review) and must be registered as a GitHub Issue instead of fixed immediately. 不具合・バグ・ギャップを発見した際にGitHub Issueへ登録する場合に使用。"
---
# Defect reporting / 不具合報告
Follow the user's input language. Use the `gh` CLI (per repository preference), never the GitHub MCP tools, for all issue operations.
Use native Copilot research/review only to gather reproduction evidence; do not add another issue-tracker client, bot, or agent runtime for this task.
This skill only registers an issue; it never implements a fix. If the user asks
to both report and fix now, register the issue first, then hand off to
`sdd-change` referencing that issue number.
## 1. Confirm it is reportable, not immediately fixable / 報告対象かを確認
1. Reproduce or clearly observe the defect (failing command output, wrong
   diagnostic, unexpected file content, etc.); never file speculative reports
   without concrete evidence (command, input, actual vs. expected output).
2. Search first to avoid duplicates: `gh issue list --search "<keywords>" --state all`.
   If a matching open issue exists, add a comment with the new reproduction
   instead of creating a duplicate; if closed and it recurs, reopen with
   `gh issue reopen <number>` and comment with the new evidence.
3. If the defect is trivial and the user asked to fix it now (not just report
   it), do not use this skill — use `sdd-change` directly instead.
## 2. Write the report / 報告内容を作成
1. Title: short, specific, symptom-first (e.g. "`config lint` flags Go's
   `./...` wildcard as a missing path"). Avoid vague titles like "bug in CLI".
2. Body must include, as separate sections:
   - **Summary**: what is wrong, one or two sentences.
   - **Reproduction**: exact command(s), input file/config content, and
     environment (musubix3 version, adapter, OS) needed to reproduce.
   - **Actual vs Expected**: observed output vs. what should happen.
   - **Impact**: who/what this blocks (false positive, silent failure,
     onboarding confusion, severity estimate).
   - **Suggested fix** (optional): only if you have a concrete idea; do not
     guess at internals you have not inspected.
3. Never include secrets, credentials, or third-party confidential data in the
   issue body, even when they appear in a reproduction log.
4. Create with `gh issue create --title "<title>" --body "<body>"`; apply
   labels only if the repository defines them (`gh label list`), never invent
   new labels ad hoc without asking.
## 3. Confirm and hand off / 確認と連携
1. Print the created issue URL/number back to the user.
2. If the user wants it fixed now or later, note that a future `sdd-change`
   commit should use `Fixes #<number>` (one `Fixes` keyword per issue number
   when closing several in one commit) so GitHub auto-closes it on merge to
   the default branch.
3. Do not mark the issue as fixed, closed, or resolved yourself unless the
   user explicitly confirms a fix has already been merged.
