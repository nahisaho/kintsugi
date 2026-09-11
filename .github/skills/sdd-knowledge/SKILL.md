---
name: sdd-knowledge
description: "Use when retrieving local SDD decisions, requirements and Git co-change evidence with deterministic ranked search. 仕様・設計判断・Git根拠の検索時に使用。"
---
# Knowledge / 知識

Follow the user's input language (日本語 / English). Use Copilot's native memory
for conversational/project memory and native research for external information.
This tool indexes local evidence; it is not another memory or research service.
After the work, run `npx musubix3 workflow-record sdd-knowledge complete --status
completed` exactly once.

1. Run `npx musubix3 knowledge build`. Markdown artifacts and the last 100 Git
   commits are indexed locally in `.musubix/cache/knowledge.json`.
2. Query `npx musubix3 knowledge query "your requirement or decision" --json`.
   Ranking is deterministic TF-IDF/cosine with Japanese bigram tokenization,
   not GraphRAG, embeddings, learned expertise or a semantic truth engine.
3. Read returned source paths before making claims. Scores indicate lexical
   similarity only. Git co-change is correlation; author-directory counts are
   contributions, not ownership or expertise assessments.
4. Rebuild if `stale` is true. Git evidence can be explicitly skipped in an empty
   repository or when Git is unavailable. Never invent historical evidence.
5. Use native research only when local sources are insufficient, then native
   editing to record confirmed decisions as ADRs. Keep cache data local.
