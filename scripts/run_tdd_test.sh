#!/usr/bin/env bash
# Runs exactly one doctest TEST-* case and writes a fresh musubix-json report
# scoped to that single test, for musubix3 `tdd red`/`tdd green` evidence.
set -euo pipefail

TEST_ID="${1:-}"
if [ -z "$TEST_ID" ]; then
  # Invoked with base (non-TDD) args during a general gate/command run: this
  # command exists solely to serve `tdd red`/`tdd green` via {testId}. The
  # full suite is independently covered by the "cpp-tests" command, so a
  # bare invocation is a documented no-op rather than a usage failure.
  echo "run_tdd_test.sh: no TEST-ID supplied; no-op (full suite covered by cpp-tests)."
  exit 0
fi
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
REPORT_DIR="$ROOT_DIR/.musubix/reports/tdd"
REPORT_PATH="$REPORT_DIR/${TEST_ID}.json"
RAW_OUTPUT_PATH="$REPORT_DIR/${TEST_ID}.out"

mkdir -p "$REPORT_DIR"
rm -f "$REPORT_PATH" "$RAW_OUTPUT_PATH"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug > "$REPORT_DIR/${TEST_ID}.configure.log" 2>&1
cmake --build "$BUILD_DIR" --target pottery_tests -j"$(nproc)" > "$REPORT_DIR/${TEST_ID}.build.log" 2>&1

set +e
"$BUILD_DIR/pottery_tests" --test-case="${TEST_ID}*" > "$RAW_OUTPUT_PATH" 2>&1
EXIT_CODE=$?
set -e

STATUS="failed"
if grep -Eq '\[doctest\][[:space:]]+test cases:[[:space:]]*1[[:space:]]*\|[[:space:]]*1 passed[[:space:]]*\|[[:space:]]*0 failed' "$RAW_OUTPUT_PATH"; then
  if [ "$EXIT_CODE" -eq 0 ]; then
    STATUS="passed"
  fi
elif grep -Eq '\[doctest\][[:space:]]+test cases:[[:space:]]*1[[:space:]]*\|.*\|[[:space:]]*[1-9][0-9]* failed' "$RAW_OUTPUT_PATH"; then
  STATUS="failed"
else
  # Zero or more-than-one test case matched: cannot trust the result as a
  # single-test signal, so treat it as an error (never a silent pass).
  STATUS="error"
fi

python3 - "$REPORT_PATH" "$TEST_ID" "$STATUS" <<'PYEOF'
import json
import sys

path, test_id, status = sys.argv[1], sys.argv[2], sys.argv[3]
report = {"schemaVersion": 1, "tests": [{"id": test_id, "status": status}]}
with open(path, "w", encoding="utf-8") as f:
    json.dump(report, f)
PYEOF

echo "musubix-json report written to $REPORT_PATH (status=$STATUS)"
# Mirror the underlying test binary's own exit code: musubix3 tdd red/green
# validates BOTH the report status AND the command's process exit code
# (nonzero expected for red, zero expected for green/refactor).
exit "$EXIT_CODE"
