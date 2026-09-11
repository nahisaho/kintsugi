---
version: 1.0.0
---
# Project constitution / プロジェクト憲章

## PRINC-001: Evidence before claims / 主張には根拠を
### RULE-001: Trace mandatory requirements / 必須要求を追跡する
Metric: trace.errors
Limit: 0

## PRINC-002: Run real checks / 実際に検証する
### RULE-002: No failed verification commands / 検証失敗を許可しない
Metric: commands.failures
Limit: 0
### RULE-003: Do not pass skipped checks / 未実行を成功扱いしない
Metric: commands.skipped
Limit: 0
