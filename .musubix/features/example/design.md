---
schemaVersion: 1
feature: example
---
# Design / 設計

## DES-EXAMPLE-001: Readiness component / 準備状況コンポーネント
Responsibilities: Aggregate explicit readiness evidence without inventing success.
Interfaces: reportReadiness() returns pass, fail, or skipped evidence.
Constraints: Missing required evidence cannot count as success.
Requirements: REQ-EXAMPLE-001
ADRs: ADR-0001
Depends-On: none
